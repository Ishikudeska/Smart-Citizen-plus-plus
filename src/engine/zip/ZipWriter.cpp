#include "engine/zip/ZipWriter.h"

#include "engine/Try.h"

#include <zlib.h>

#include <algorithm>

namespace engine::zip {

namespace {

constexpr std::uint16_t kVersion = 20;
constexpr std::uint16_t kUtf8Names = 1 << 11;

void put16(std::vector<std::uint8_t> &out, std::uint16_t v)
{
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void put32(std::vector<std::uint8_t> &out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

void dosDateTime(std::time_t t, std::uint16_t &time, std::uint16_t &date)
{
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    const int year = std::max(tm.tm_year + 1900, 1980);
    time = static_cast<std::uint16_t>((tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2));
    date = static_cast<std::uint16_t>(((year - 1980) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday);
}

Result<std::vector<std::uint8_t>> deflateRaw(std::span<const std::uint8_t> data, int level)
{
    z_stream zs{};
    if (deflateInit2(&zs, level, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return fail(Errc::Io, "zlib initialization failed");
    std::vector<std::uint8_t> out(deflateBound(&zs, static_cast<uLong>(data.size())));
    zs.next_in = const_cast<Bytef *>(data.data());
    zs.avail_in = static_cast<uInt>(data.size());
    zs.next_out = out.data();
    zs.avail_out = static_cast<uInt>(out.size());
    const int rc = deflate(&zs, Z_FINISH);
    const std::size_t produced = zs.total_out;
    deflateEnd(&zs);
    if (rc != Z_STREAM_END)
        return fail(Errc::Io, "deflate failed");
    out.resize(produced);
    return out;
}

} // namespace

Result<ZipWriter> ZipWriter::create(const std::filesystem::path &path)
{
    auto file = io::OutputFile::create(path);
    if (!file)
        return std::unexpected(file.error());
    ZipWriter writer;
    writer.file_ = std::move(*file);
    return writer;
}

Result<void> ZipWriter::write(std::span<const std::uint8_t> bytes)
{
    SC_TRY(file_.write(bytes));
    offset_ += bytes.size();
    return {};
}

Result<void> ZipWriter::add(std::string_view name, std::span<const std::uint8_t> data, int level,
                            std::time_t modified)
{
    if (data.size() > 0xFFFFFFFEu || offset_ > 0xFFFFFFFEu || entries_.size() >= 0xFFFF)
        return fail(Errc::Unsupported, "ZIP64 output is not supported");

    Entry e;
    e.name = std::string(name);
    e.size = static_cast<std::uint32_t>(data.size());
    e.crc = static_cast<std::uint32_t>(crc32(0, data.data(), static_cast<uInt>(data.size())));
    e.offset = static_cast<std::uint32_t>(offset_);
    dosDateTime(modified, e.dosTime, e.dosDate);

    std::vector<std::uint8_t> compressed;
    std::span<const std::uint8_t> payload = data;
    if (level > 0) {
        auto deflated = deflateRaw(data, level);
        if (!deflated)
            return std::unexpected(deflated.error());
        compressed = std::move(*deflated);
        payload = compressed;
        e.method = 8;
    }
    e.compressedSize = static_cast<std::uint32_t>(payload.size());

    std::vector<std::uint8_t> header;
    put32(header, 0x04034b50);
    put16(header, kVersion);
    put16(header, kUtf8Names);
    put16(header, e.method);
    put16(header, e.dosTime);
    put16(header, e.dosDate);
    put32(header, e.crc);
    put32(header, e.compressedSize);
    put32(header, e.size);
    put16(header, static_cast<std::uint16_t>(e.name.size()));
    put16(header, 0);
    header.insert(header.end(), e.name.begin(), e.name.end());
    SC_TRY(write(header));
    SC_TRY(write(payload));
    entries_.push_back(std::move(e));
    return {};
}

Result<void> ZipWriter::finish()
{
    const std::uint64_t directoryOffset = offset_;
    std::vector<std::uint8_t> dir;
    for (const Entry &e : entries_) {
        put32(dir, 0x02014b50);
        put16(dir, kVersion);
        put16(dir, kVersion);
        put16(dir, kUtf8Names);
        put16(dir, e.method);
        put16(dir, e.dosTime);
        put16(dir, e.dosDate);
        put32(dir, e.crc);
        put32(dir, e.compressedSize);
        put32(dir, e.size);
        put16(dir, static_cast<std::uint16_t>(e.name.size()));
        put16(dir, 0); // extra
        put16(dir, 0); // comment
        put16(dir, 0); // disk
        put16(dir, 0); // internal attributes
        put32(dir, 0); // external attributes
        put32(dir, e.offset);
        dir.insert(dir.end(), e.name.begin(), e.name.end());
    }
    const std::size_t directorySize = dir.size();
    if (directoryOffset + directorySize > 0xFFFFFFFEu)
        return fail(Errc::Unsupported, "ZIP64 output is not supported");
    put32(dir, 0x06054b50);
    put16(dir, 0);
    put16(dir, 0);
    put16(dir, static_cast<std::uint16_t>(entries_.size()));
    put16(dir, static_cast<std::uint16_t>(entries_.size()));
    put32(dir, static_cast<std::uint32_t>(directorySize));
    put32(dir, static_cast<std::uint32_t>(directoryOffset));
    put16(dir, 0);
    SC_TRY(write(dir));
    return file_.commit();
}

} // namespace engine::zip
