#include "engine/p4k/Archive.h"

#include "engine/Try.h"
#include "engine/crypto/Aes128.h"

#include <zlib.h>
#include <zstd.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace engine::p4k {

namespace {

constexpr std::uint32_t kLocalHeaderSig = 0x04034b50;    // PK\3\4
constexpr std::uint32_t kCigLocalHeaderSig = 0x14034b50; // PK\3\x14, accepted by unp4k, unused today
constexpr std::uint32_t kCentralHeaderSig = 0x02014b50;
constexpr std::uint32_t kEndOfCentralDirSig = 0x06054b50;
constexpr std::uint32_t kZip64EndOfCentralDirSig = 0x06064b50;
constexpr std::uint32_t kZip64LocatorSig = 0x07064b50;

constexpr std::size_t kCentralHeaderSize = 46;
constexpr std::size_t kLocalHeaderSize = 30;
constexpr std::size_t kEndOfCentralDirSize = 22;
constexpr std::size_t kZip64EndOfCentralDirSize = 56;
constexpr std::size_t kZip64LocatorSize = 20;

constexpr std::uint16_t kTagZip64 = 0x0001;
constexpr std::uint16_t kTagCigEncryption = 0x5002;
constexpr std::uint16_t kTagCigSha256 = 0x5003;

constexpr std::size_t kReadChunk = 1 << 20; // multiple of the AES block size
constexpr std::size_t kDirectoryChunk = 8 << 20;
constexpr std::uint8_t kZstdMagic[4] = {0x28, 0xB5, 0x2F, 0xFD};

inline std::uint16_t u16(const std::uint8_t *p)
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

inline std::uint32_t u32(const std::uint8_t *p)
{
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) |
           (std::uint32_t(p[3]) << 24);
}

inline std::uint64_t u64(const std::uint8_t *p)
{
    return std::uint64_t(u32(p)) | (std::uint64_t(u32(p + 4)) << 32);
}

inline char foldChar(char c)
{
    if (c == '\\')
        return '/';
    if (c >= 'A' && c <= 'Z')
        return static_cast<char>(c + ('a' - 'A'));
    return c;
}

std::uint64_t hashName(std::string_view s)
{
    std::uint64_t h = 1469598103934665603ull;
    for (char c : s) {
        h ^= static_cast<std::uint8_t>(foldChar(c));
        h *= 1099511628211ull;
    }
    return h;
}

bool sameName(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (foldChar(a[i]) != foldChar(b[i]))
            return false;
    return true;
}

// --- Extra fields ---------------------------------------------------------

struct ExtraField
{
    std::uint16_t tag = 0;
    std::span<const std::uint8_t> data;
};

struct ExtraFields
{
    std::array<ExtraField, 8> items{};
    std::size_t count = 0;
    bool tiles = false; // the fields cover the extra data exactly

    const ExtraField *find(std::uint16_t tag) const
    {
        for (std::size_t i = 0; i < count; ++i)
            if (items[i].tag == tag)
                return &items[i];
        return nullptr;
    }
};

// CIG's lengths include the 4-byte tag+length header; standard ZIP's don't.
ExtraFields splitExtra(std::span<const std::uint8_t> extra, bool lengthIncludesHeader)
{
    ExtraFields fields;
    std::size_t pos = 0;
    while (pos + 4 <= extra.size()) {
        const std::uint16_t tag = u16(&extra[pos]);
        const std::uint16_t len = u16(&extra[pos + 2]);
        std::size_t next = 0;
        if (lengthIncludesHeader) {
            if (len < 4)
                return fields;
            next = pos + len;
        } else {
            next = pos + 4 + len;
        }
        if (next > extra.size())
            return fields;
        if (fields.count < fields.items.size())
            fields.items[fields.count++] = {tag, extra.subspan(pos + 4, next - pos - 4)};
        pos = next;
    }
    fields.tiles = pos == extra.size();
    return fields;
}

ExtraFields parseExtra(std::span<const std::uint8_t> extra)
{
    ExtraFields cig = splitExtra(extra, true);
    if (cig.tiles && (cig.find(kTagCigEncryption) || cig.find(kTagCigSha256)))
        return cig;
    return splitExtra(extra, false);
}

// --- Decoders -------------------------------------------------------------

std::vector<std::uint8_t> &outputBuffer()
{
    thread_local std::vector<std::uint8_t> buffer(256 * 1024);
    return buffer;
}

class Decoder
{
public:
    virtual ~Decoder() = default;
    virtual Result<void> push(std::span<const std::uint8_t> in) = 0;
    virtual Result<void> finish() = 0;
};

// Passes bytes through. For decrypted data it drops trailing zero bytes, as
// the C# does after decrypting (the AES padding is zeros).
class StoredDecoder final : public Decoder
{
public:
    StoredDecoder(const Sink &out, bool stripTrailingZeros)
        : out_(out)
        , strip_(stripTrailingZeros)
    {
    }

    Result<void> push(std::span<const std::uint8_t> in) override
    {
        if (!strip_)
            return in.empty() ? Result<void>{} : out_(in);

        std::size_t keep = in.size();
        while (keep > 0 && in[keep - 1] == 0)
            --keep;
        if (keep == 0) {
            pendingZeros_ += in.size();
            return {};
        }
        SC_TRY(flushZeros());
        SC_TRY(out_(in.first(keep)));
        pendingZeros_ = in.size() - keep;
        return {};
    }

    Result<void> finish() override { return {}; }

private:
    Result<void> flushZeros()
    {
        static constexpr std::uint8_t kZeros[4096] = {};
        while (pendingZeros_ > 0) {
            const std::size_t n = std::min<std::uint64_t>(pendingZeros_, sizeof kZeros);
            SC_TRY(out_(std::span(kZeros, n)));
            pendingZeros_ -= n;
        }
        return {};
    }

    const Sink &out_;
    bool strip_;
    std::uint64_t pendingZeros_ = 0;
};

class InflateDecoder final : public Decoder
{
public:
    explicit InflateDecoder(const Sink &out)
        : out_(out)
    {
        ok_ = inflateInit2(&zs_, -MAX_WBITS) == Z_OK;
    }

    ~InflateDecoder() override
    {
        if (ok_)
            inflateEnd(&zs_);
    }

    Result<void> push(std::span<const std::uint8_t> in) override
    {
        if (!ok_)
            return fail(Errc::Io, "zlib initialization failed");
        if (done_)
            return {};
        auto &buf = outputBuffer();
        zs_.next_in = const_cast<Bytef *>(in.data());
        zs_.avail_in = static_cast<uInt>(in.size());
        for (;;) {
            zs_.next_out = buf.data();
            zs_.avail_out = static_cast<uInt>(buf.size());
            const int rc = inflate(&zs_, Z_NO_FLUSH);
            if (rc != Z_OK && rc != Z_STREAM_END && rc != Z_BUF_ERROR)
                return fail(Errc::Corrupt, std::string("deflate: ") + (zs_.msg ? zs_.msg : "invalid data"));
            const std::size_t produced = buf.size() - zs_.avail_out;
            if (produced > 0)
                SC_TRY(out_(std::span(buf.data(), produced)));
            if (rc == Z_STREAM_END) {
                done_ = true;
                return {};
            }
            if (zs_.avail_in == 0 && zs_.avail_out != 0)
                return {};
            if (rc == Z_BUF_ERROR && produced == 0)
                return {};
        }
    }

    Result<void> finish() override
    {
        if (!done_)
            return fail(Errc::Corrupt, "deflate stream is truncated");
        return {};
    }

private:
    const Sink &out_;
    z_stream zs_{};
    bool ok_ = false;
    bool done_ = false;
};

ZSTD_DCtx *zstdContext()
{
    struct Holder
    {
        ZSTD_DCtx *ctx = ZSTD_createDCtx();
        ~Holder() { ZSTD_freeDCtx(ctx); }
    };
    thread_local Holder holder;
    ZSTD_DCtx_reset(holder.ctx, ZSTD_reset_session_only);
    return holder.ctx;
}

// Method 100. unp4k checks for the zstd magic and passes the bytes through
// unchanged when it is missing. Decodes exactly one frame: encrypted entries
// carry up to 15 zero bytes of AES padding after it.
class ZstdDecoder final : public Decoder
{
public:
    ZstdDecoder(const Sink &out, bool encrypted)
        : out_(out)
        , passthrough_(out, encrypted)
    {
    }

    Result<void> push(std::span<const std::uint8_t> in) override
    {
        if (state_ == State::Sniffing) {
            const std::size_t take = std::min(in.size(), head_.size() - headLen_);
            std::memcpy(head_.data() + headLen_, in.data(), take);
            headLen_ += take;
            in = in.subspan(take);
            if (headLen_ < head_.size())
                return {};
            if (std::memcmp(head_.data(), kZstdMagic, 4) == 0) {
                state_ = State::Frame;
                ctx_ = zstdContext();
                SC_TRY(decompress(std::span<const std::uint8_t>(head_.data(), headLen_)));
            } else {
                state_ = State::Passthrough;
                SC_TRY(passthrough_.push(std::span<const std::uint8_t>(head_.data(), headLen_)));
            }
        }
        switch (state_) {
        case State::Frame:
            return decompress(in);
        case State::Passthrough:
            return passthrough_.push(in);
        default:
            return {};
        }
    }

    Result<void> finish() override
    {
        switch (state_) {
        case State::Sniffing:
            SC_TRY(passthrough_.push(std::span<const std::uint8_t>(head_.data(), headLen_)));
            return passthrough_.finish();
        case State::Passthrough:
            return passthrough_.finish();
        case State::Frame:
            SC_TRY(decompress({}));
            if (state_ != State::Done)
                return fail(Errc::Corrupt, "zstd frame is truncated");
            return {};
        case State::Done:
            return {};
        }
        return {};
    }

private:
    enum class State { Sniffing, Frame, Passthrough, Done };

    Result<void> decompress(std::span<const std::uint8_t> in)
    {
        auto &buf = outputBuffer();
        ZSTD_inBuffer zin{in.data(), in.size(), 0};
        while (state_ == State::Frame) {
            ZSTD_outBuffer zout{buf.data(), buf.size(), 0};
            const std::size_t rc = ZSTD_decompressStream(ctx_, &zout, &zin);
            if (ZSTD_isError(rc))
                return fail(Errc::Corrupt, std::string("zstd: ") + ZSTD_getErrorName(rc));
            if (zout.pos > 0)
                SC_TRY(out_(std::span(buf.data(), zout.pos)));
            if (rc == 0)
                state_ = State::Done;
            else if (zin.pos == zin.size && zout.pos < zout.size)
                break; // needs more input
        }
        return {};
    }

    const Sink &out_;
    StoredDecoder passthrough_;
    State state_ = State::Sniffing;
    std::array<std::uint8_t, 4> head_{};
    std::size_t headLen_ = 0;
    ZSTD_DCtx *ctx_ = nullptr;
};

std::vector<std::uint8_t> &readBuffer()
{
    thread_local std::vector<std::uint8_t> buffer;
    return buffer;
}

} // namespace

std::string_view methodName(std::uint16_t method)
{
    switch (method) {
    case 0:
        return "Stored";
    case 8:
        return "Deflated";
    case 100:
        return "ZStd";
    default:
        return "Unknown";
    }
}

Result<std::shared_ptr<const Archive>> Archive::open(const std::filesystem::path &path,
                                                    const OpenOptions &options)
{
    auto file = io::RandomAccessFile::open(path);
    if (!file)
        return std::unexpected(file.error());

    std::shared_ptr<Archive> archive(new Archive());
    archive->file_ = std::move(*file);
    archive->key_ = options.key;
    SC_TRY(archive->parseDirectory(options));
    return std::shared_ptr<const Archive>(std::move(archive));
}

std::string_view Archive::name(std::size_t index) const
{
    const Entry &e = entries_[index];
    return std::string_view(names_.data() + e.nameOffset, e.nameLength);
}

Result<void> Archive::parseDirectory(const OpenOptions &options)
{
    const std::uint64_t size = file_.size();
    const std::string where = file_.path().string();
    if (size < kEndOfCentralDirSize)
        return fail(Errc::Format, where + " is not a ZIP/P4K archive");

    // Scan back for the end record. Data.p4k has a 16-byte "CIG" comment and
    // then 828 zero bytes after it, so it is not at the very end.
    const std::size_t tailSize = static_cast<std::size_t>(std::min<std::uint64_t>(size, 1 << 20));
    const std::uint64_t tailPos = size - tailSize;
    std::vector<std::uint8_t> tail(tailSize);
    SC_TRY(file_.readAt(tailPos, tail));

    std::optional<std::size_t> eocd;
    for (std::size_t i = tailSize - kEndOfCentralDirSize + 1; i-- > 0;) {
        if (u32(&tail[i]) == kEndOfCentralDirSig && i + kEndOfCentralDirSize + u16(&tail[i + 20]) <= tailSize) {
            eocd = i;
            break;
        }
    }
    if (!eocd)
        return fail(Errc::Format, where + " is not a ZIP/P4K archive (no end of central directory)");

    const std::uint8_t *end = &tail[*eocd];
    std::uint64_t entryCount = u16(end + 10);
    std::uint64_t directoryOffset = u32(end + 16);
    std::uint64_t directoryEnd = tailPos + *eocd;

    if (*eocd >= kZip64LocatorSize && u32(&tail[*eocd - kZip64LocatorSize]) == kZip64LocatorSig) {
        const std::uint64_t zip64Pos = u64(&tail[*eocd - kZip64LocatorSize + 8]);
        if (zip64Pos > size - kZip64EndOfCentralDirSize)
            return fail(Errc::Format, where + ": bad ZIP64 locator");
        std::uint8_t z[kZip64EndOfCentralDirSize];
        SC_TRY(file_.readAt(zip64Pos, z));
        if (u32(z) != kZip64EndOfCentralDirSig)
            return fail(Errc::Format, where + ": bad ZIP64 end of central directory");
        entryCount = u64(z + 32);
        directoryOffset = u64(z + 48);
        // The directory size field (z + 40) is 1,024 bytes too large in LIVE's
        // Data.p4k, so bound the directory by where the ZIP64 record starts.
        directoryEnd = zip64Pos;
    } else if (entryCount == 0xFFFF || u32(end + 16) == 0xFFFFFFFF) {
        return fail(Errc::Format, where + ": ZIP64 archive without a ZIP64 locator");
    }

    if (directoryOffset > directoryEnd)
        return fail(Errc::Format, where + ": central directory offset is out of range");
    const std::uint64_t directoryBytes = directoryEnd - directoryOffset;
    if (entryCount > directoryBytes / kCentralHeaderSize)
        return fail(Errc::Format, where + ": central directory is too small for its entry count");

    centralDirectoryOffset_ = directoryOffset;
    entries_.reserve(static_cast<std::size_t>(entryCount));
    names_.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(directoryBytes, entryCount * 128)));

    // Stream the directory through a bounded buffer: one pass, no 443 MB copy.
    std::vector<std::uint8_t> buf(kDirectoryChunk);
    std::size_t begin = 0;
    std::size_t filled = 0;
    std::uint64_t readPos = directoryOffset;

    auto ensure = [&](std::size_t need) -> Result<bool> {
        if (filled - begin >= need)
            return true;
        std::memmove(buf.data(), buf.data() + begin, filled - begin);
        filled -= begin;
        begin = 0;
        if (need > buf.size())
            buf.resize(need);
        const std::size_t want =
            static_cast<std::size_t>(std::min<std::uint64_t>(buf.size() - filled, directoryEnd - readPos));
        if (want > 0) {
            SC_TRY(file_.readAt(readPos, std::span(buf.data() + filled, want)));
            filled += want;
            readPos += want;
            if (options.progress)
                options.progress(readPos - directoryOffset, directoryBytes);
        }
        return filled - begin >= need;
    };

    for (std::uint64_t i = 0; i < entryCount; ++i) {
        if (options.cancel && options.cancel->load(std::memory_order_relaxed))
            return fail(Errc::Cancelled, "cancelled");

        auto have = ensure(kCentralHeaderSize);
        if (!have)
            return std::unexpected(have.error());
        if (!*have)
            return fail(Errc::Format, where + ": central directory is truncated");
        if (u32(buf.data() + begin) != kCentralHeaderSig)
            return fail(Errc::Format, where + ": bad central directory record " + std::to_string(i));

        const std::size_t nameLen = u16(buf.data() + begin + 28);
        const std::size_t extraLen = u16(buf.data() + begin + 30);
        const std::size_t commentLen = u16(buf.data() + begin + 32);
        const std::size_t recordLen = kCentralHeaderSize + nameLen + extraLen + commentLen;
        have = ensure(recordLen);
        if (!have)
            return std::unexpected(have.error());
        if (!*have)
            return fail(Errc::Format, where + ": central directory is truncated");

        const std::uint8_t *h = buf.data() + begin;
        const std::uint64_t recordPos = readPos - (filled - begin);

        Entry e;
        e.flags = u16(h + 8);
        e.method = u16(h + 10);
        e.dosDateTime = u32(h + 12);
        e.crc32 = u32(h + 16);
        const std::uint32_t compressed32 = u32(h + 20);
        const std::uint32_t uncompressed32 = u32(h + 24);
        const std::uint32_t offset32 = u32(h + 42);
        e.compressedSize = compressed32;
        e.uncompressedSize = uncompressed32;
        e.localHeaderOffset = offset32;
        const std::uint64_t relative = recordPos - directoryOffset;
        e.centralOffset = relative <= 0xFFFFFFFFu ? static_cast<std::uint32_t>(relative) : 0xFFFFFFFFu;

        if (names_.size() + nameLen > 0xFFFFFFFFu)
            return fail(Errc::Unsupported, where + ": entry names exceed 4 GB");
        e.nameOffset = static_cast<std::uint32_t>(names_.size());
        e.nameLength = static_cast<std::uint16_t>(nameLen);
        for (std::size_t k = 0; k < nameLen; ++k) {
            const char c = static_cast<char>(h[kCentralHeaderSize + k]);
            names_.push_back(c == '\\' ? '/' : c);
        }

        const ExtraFields extra = parseExtra(std::span(h + kCentralHeaderSize + nameLen, extraLen));
        if (const ExtraField *zip64 = extra.find(kTagZip64)) {
            std::size_t p = 0;
            auto take = [&](std::uint64_t &value) {
                if (p + 8 > zip64->data.size())
                    return false;
                value = u64(zip64->data.data() + p);
                p += 8;
                return true;
            };
            if ((uncompressed32 == 0xFFFFFFFFu && !take(e.uncompressedSize)) ||
                (compressed32 == 0xFFFFFFFFu && !take(e.compressedSize)) ||
                (offset32 == 0xFFFFFFFFu && !take(e.localHeaderOffset)))
                return fail(Errc::Format, where + ": short ZIP64 field in record " + std::to_string(i));
        } else if (uncompressed32 == 0xFFFFFFFFu || compressed32 == 0xFFFFFFFFu || offset32 == 0xFFFFFFFFu) {
            return fail(Errc::Format, where + ": missing ZIP64 field in record " + std::to_string(i));
        }
        if (const ExtraField *crypt = extra.find(kTagCigEncryption); crypt && !crypt->data.empty())
            e.encrypted = crypt->data[0] != 0;

        entries_.push_back(e);
        begin += recordLen;
    }

    names_.shrink_to_fit();
    return {};
}

void Archive::buildLookup() const
{
    std::size_t capacity = 16;
    while (capacity < entries_.size() * 2)
        capacity <<= 1;
    lookup_.assign(capacity, 0xFFFFFFFFu);
    const std::size_t mask = capacity - 1;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const std::string_view n = name(i);
        std::size_t slot = hashName(n) & mask;
        for (;;) {
            const std::uint32_t occupant = lookup_[slot];
            if (occupant == 0xFFFFFFFFu) {
                lookup_[slot] = static_cast<std::uint32_t>(i);
                break;
            }
            if (sameName(name(occupant), n))
                break; // keep the first, as SharpZipLib's FindEntry does
            slot = (slot + 1) & mask;
        }
    }
}

std::optional<std::size_t> Archive::find(std::string_view path) const
{
    std::call_once(lookupOnce_, [this] { buildLookup(); });
    const std::size_t mask = lookup_.size() - 1;
    std::size_t slot = hashName(path) & mask;
    for (;;) {
        const std::uint32_t occupant = lookup_[slot];
        if (occupant == 0xFFFFFFFFu)
            return std::nullopt;
        if (sameName(name(occupant), path))
            return occupant;
        slot = (slot + 1) & mask;
    }
}

Result<std::uint64_t> Archive::dataOffset(const Entry &entry) const
{
    std::uint8_t h[kLocalHeaderSize];
    SC_TRY(file_.readAt(entry.localHeaderOffset, h));
    const std::uint32_t sig = u32(h);
    if (sig != kLocalHeaderSig && sig != kCigLocalHeaderSig)
        return fail(Errc::Corrupt, "bad local header for " +
                                       std::string(names_.data() + entry.nameOffset, entry.nameLength));
    const std::uint64_t offset = entry.localHeaderOffset + kLocalHeaderSize + u16(h + 26) + u16(h + 28);
    if (offset > file_.size() || entry.compressedSize > file_.size() - offset)
        return fail(Errc::Corrupt, "entry data runs past the end of the archive: " +
                                       std::string(names_.data() + entry.nameOffset, entry.nameLength));
    return offset;
}

Result<void> Archive::readStored(std::size_t index, const Sink &sink) const
{
    const Entry &e = entries_[index];
    const auto offset = dataOffset(e);
    if (!offset)
        return std::unexpected(offset.error());

    auto &buf = readBuffer();
    buf.resize(static_cast<std::size_t>(std::min<std::uint64_t>(e.compressedSize, kReadChunk)));
    std::uint64_t done = 0;
    while (done < e.compressedSize) {
        const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(e.compressedSize - done, kReadChunk));
        SC_TRY(file_.readAt(*offset + done, std::span(buf.data(), n)));
        SC_TRY(sink(std::span<const std::uint8_t>(buf.data(), n)));
        done += n;
    }
    return {};
}

Result<void> Archive::read(std::size_t index, const Sink &sink) const
{
    const Entry &e = entries_[index];
    const std::string_view entryName = name(index);
    if ((e.flags & 1) != 0)
        return fail(Errc::Unsupported, std::string(entryName) + " uses PKWARE encryption");
    if (e.encrypted && e.compressedSize % 16 != 0)
        return fail(Errc::Corrupt, std::string(entryName) + ": encrypted size is not a multiple of 16");

    std::uint64_t produced = 0;
    const Sink counted = [&](std::span<const std::uint8_t> bytes) -> Result<void> {
        produced += bytes.size();
        if (produced > e.uncompressedSize)
            return fail(Errc::Corrupt, std::string(entryName) + " decodes to more than its recorded size");
        return sink(bytes);
    };

    std::unique_ptr<Decoder> decoder;
    switch (e.method) {
    case static_cast<std::uint16_t>(Method::Stored):
        decoder = std::make_unique<StoredDecoder>(counted, e.encrypted);
        break;
    case static_cast<std::uint16_t>(Method::Deflated):
        decoder = std::make_unique<InflateDecoder>(counted);
        break;
    case static_cast<std::uint16_t>(Method::Zstd):
        decoder = std::make_unique<ZstdDecoder>(counted, e.encrypted);
        break;
    default:
        return fail(Errc::Unsupported,
                    std::string(entryName) + " uses compression method " + std::to_string(e.method));
    }

    const auto offset = dataOffset(e);
    if (!offset)
        return std::unexpected(offset.error());

    std::optional<crypto::Aes128> aes;
    crypto::Aes128::Block iv{};
    if (e.encrypted)
        aes.emplace(key_);

    auto &buf = readBuffer();
    buf.resize(static_cast<std::size_t>(std::min<std::uint64_t>(e.compressedSize, kReadChunk)));
    std::uint64_t done = 0;
    while (done < e.compressedSize) {
        const std::size_t n = static_cast<std::size_t>(std::min<std::uint64_t>(e.compressedSize - done, kReadChunk));
        SC_TRY(file_.readAt(*offset + done, std::span(buf.data(), n)));
        if (aes)
            crypto::cbcDecrypt(*aes, iv, std::span(buf.data(), n));
        SC_TRY(decoder->push(std::span<const std::uint8_t>(buf.data(), n)));
        done += n;
    }
    SC_TRY(decoder->finish());

    if (produced != e.uncompressedSize)
        return fail(Errc::Corrupt, std::string(entryName) + " decoded to " + std::to_string(produced) +
                                       " bytes, expected " + std::to_string(e.uncompressedSize));
    return {};
}

Result<std::vector<std::uint8_t>> Archive::read(std::size_t index) const
{
    std::vector<std::uint8_t> out;
    out.reserve(static_cast<std::size_t>(entries_[index].uncompressedSize));
    SC_TRY(read(index, [&out](std::span<const std::uint8_t> bytes) -> Result<void> {
        out.insert(out.end(), bytes.begin(), bytes.end());
        return {};
    }));
    return out;
}

Result<std::optional<crypto::Sha256::Digest>> Archive::storedHash(std::size_t index) const
{
    const Entry &e = entries_[index];
    if (e.centralOffset == 0xFFFFFFFFu)
        return std::optional<crypto::Sha256::Digest>{};

    const std::uint64_t pos = centralDirectoryOffset_ + e.centralOffset;
    std::uint8_t h[kCentralHeaderSize];
    SC_TRY(file_.readAt(pos, h));
    if (u32(h) != kCentralHeaderSig)
        return fail(Errc::Corrupt, "bad central directory record for " + std::string(name(index)));
    std::vector<std::uint8_t> extra(u16(h + 30));
    SC_TRY(file_.readAt(pos + kCentralHeaderSize + u16(h + 28), extra));

    const ExtraFields fields = parseExtra(extra);
    const ExtraField *sha = fields.find(kTagCigSha256);
    if (!sha || sha->data.size() < 32)
        return std::optional<crypto::Sha256::Digest>{};
    crypto::Sha256::Digest digest{};
    std::memcpy(digest.data(), sha->data.data(), 32);
    return std::optional(digest);
}

} // namespace engine::p4k
