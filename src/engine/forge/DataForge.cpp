#include "engine/forge/DataForge.h"

#include "engine/xml/DotNetXmlWriter.h"

#include <cstring>

namespace engine::forge {

namespace {

constexpr std::uint64_t kHeaderSize = 0x78;
constexpr std::uint64_t kLegacySizeLimit = 0x0e2e00;

// Entry sizes of the value pools, in Pool order.
constexpr std::uint64_t kPoolEntrySize[] = {
    1,  // Int8
    2,  // Int16
    4,  // Int32
    8,  // Int64
    1,  // UInt8
    2,  // UInt16
    4,  // UInt32
    8,  // UInt64
    1,  // Boolean
    4,  // Single
    8,  // Double
    16, // Guid
    4,  // String
    4,  // Locale
    4,  // Enum
    8,  // StrongPointer
    8,  // WeakPointer
    20, // Reference
    4,  // EnumOption
};
static_assert(std::size(kPoolEntrySize) == static_cast<std::size_t>(Pool::Count));

inline std::uint16_t u16(const std::uint8_t *p)
{
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
}

inline std::uint32_t u32(const std::uint8_t *p)
{
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) |
           (std::uint32_t(p[3]) << 24);
}

std::optional<std::string_view> cStringAt(std::span<const std::uint8_t> data, std::uint64_t tableOffset,
                                          std::uint64_t tableLength, std::uint64_t offset)
{
    if (offset > tableLength)
        return std::nullopt;
    const char *begin = reinterpret_cast<const char *>(data.data() + tableOffset);
    std::uint64_t end = offset;
    while (end < tableLength && begin[end] != '\0')
        ++end;
    return std::string_view(begin + offset, static_cast<std::size_t>(end - offset));
}

std::string toUtf8(std::string_view latin1)
{
    std::string out;
    out.reserve(latin1.size());
    xml::appendLatin1AsUtf8(out, latin1);
    return out;
}

} // namespace

std::string dataTypeName(std::uint16_t type)
{
    switch (static_cast<DataType>(type)) {
    case DataType::Boolean:
        return "varBoolean";
    case DataType::Int8:
        return "varInt8";
    case DataType::Int16:
        return "varInt16";
    case DataType::Int32:
        return "varInt32";
    case DataType::Int64:
        return "varInt64";
    case DataType::UInt8:
        return "varUInt8";
    case DataType::UInt16:
        return "varUInt16";
    case DataType::UInt32:
        return "varUInt32";
    case DataType::UInt64:
        return "varUInt64";
    case DataType::String:
        return "varString";
    case DataType::Single:
        return "varSingle";
    case DataType::Double:
        return "varDouble";
    case DataType::Locale:
        return "varLocale";
    case DataType::Guid:
        return "varGuid";
    case DataType::Enum:
        return "varEnum";
    case DataType::Class:
        return "varClass";
    case DataType::StrongPointer:
        return "varStrongPointer";
    case DataType::WeakPointer:
        return "varWeakPointer";
    case DataType::Reference:
        return "varReference";
    }
    return std::to_string(type);
}

std::size_t GuidHash::operator()(GuidBytes g) const noexcept
{
    std::uint64_t a = 0, b = 0;
    std::memcpy(&a, g.data(), 8);
    std::memcpy(&b, g.data() + 8, 8);
    return static_cast<std::size_t>(a * 0x9E3779B97F4A7C15ull ^ b);
}

Result<DataForge> DataForge::load(std::vector<std::uint8_t> bytes)
{
    DataForge df;
    df.data_ = std::move(bytes);
    const std::uint64_t size = df.data_.size();
    const std::uint8_t *d = df.data_.data();

    if (size < kHeaderSize)
        return fail(Errc::Format, "not a DataForge file (too small)");
    df.version_ = static_cast<std::int32_t>(u32(d + 4));
    if (size < kLegacySizeLimit && df.version_ < 6)
        return fail(Errc::Unsupported,
                    "legacy DataForge files are not supported (version " + std::to_string(df.version_) + ")");
    if (df.version_ < 5)
        return fail(Errc::Unsupported,
                    "DataForge version " + std::to_string(df.version_) + " is not supported");

    // Header: u16, u16, i32 version, 4 x u16, then 24 i32 counts.
    std::int64_t counts[24];
    for (int i = 0; i < 24; ++i) {
        counts[i] = static_cast<std::int32_t>(u32(d + 16 + i * 4));
        if (counts[i] < 0)
            return fail(Errc::Format, "DataForge header has a negative count");
    }
    enum {
        cStruct,
        cProperty,
        cEnum,
        cMapping,
        cRecord,
        cBool,
        cI8,
        cI16,
        cI32,
        cI64,
        cU8,
        cU16,
        cU32,
        cU64,
        cSingle,
        cDouble,
        cGuid,
        cString,
        cLocale,
        cEnumValue,
        cStrong,
        cWeak,
        cReference,
        cEnumOption
    };
    df.textLength_ = u32(d + 112);
    df.blobLength_ = u32(d + 116);

    const std::uint64_t recordSize = df.version_ >= 8 ? 36 : 32;
    std::uint64_t pos = kHeaderSize;
    const std::uint64_t structsAt = pos;
    pos += std::uint64_t(counts[cStruct]) * 16;
    const std::uint64_t propertiesAt = pos;
    pos += std::uint64_t(counts[cProperty]) * 12;
    pos += std::uint64_t(counts[cEnum]) * 8; // enum definitions: not needed for export
    const std::uint64_t mappingsAt = pos;
    pos += std::uint64_t(counts[cMapping]) * 8;
    const std::uint64_t recordsAt = pos;
    pos += std::uint64_t(counts[cRecord]) * recordSize;

    // Pools are stored in a different order from the header's counts.
    const int poolCountIndex[] = {cI8,        cI16,    cI32,    cI64,       cU8,        cU16,    cU32,
                                  cU64,       cBool,   cSingle, cDouble,    cGuid,      cString, cLocale,
                                  cEnumValue, cStrong, cWeak,   cReference, cEnumOption};
    for (int p = 0; p < static_cast<int>(Pool::Count); ++p) {
        df.poolOffsets_[p] = pos;
        df.poolCounts_[p] = std::uint64_t(counts[poolCountIndex[p]]);
        pos += df.poolCounts_[p] * kPoolEntrySize[p];
    }
    df.textOffset_ = pos;
    pos += df.textLength_;
    df.blobOffset_ = pos;
    pos += df.blobLength_;
    df.dataOffset_ = pos;
    if (df.dataOffset_ > size)
        return fail(Errc::Format, "DataForge tables run past the end of the file");

    df.structs_.resize(std::size_t(counts[cStruct]));
    for (std::size_t i = 0; i < df.structs_.size(); ++i) {
        const std::uint8_t *p = d + structsAt + i * 16;
        df.structs_[i] = {u32(p), u32(p + 4), u16(p + 8), u16(p + 10), u32(p + 12)};
    }
    df.properties_.resize(std::size_t(counts[cProperty]));
    for (std::size_t i = 0; i < df.properties_.size(); ++i) {
        const std::uint8_t *p = d + propertiesAt + i * 12;
        df.properties_[i] = {u32(p), u16(p + 4), u16(p + 6), static_cast<std::uint8_t>(u16(p + 8) & 0xFF),
                             u16(p + 10)};
    }
    df.mappings_.resize(std::size_t(counts[cMapping]));
    for (std::size_t i = 0; i < df.mappings_.size(); ++i) {
        const std::uint8_t *p = d + mappingsAt + i * 8;
        df.mappings_[i] = {u32(p), u32(p + 4)};
    }
    df.records_.resize(std::size_t(counts[cRecord]));
    for (std::size_t i = 0; i < df.records_.size(); ++i) {
        const std::uint8_t *p = d + recordsAt + i * recordSize;
        RecordDef &r = df.records_[i];
        r.nameOffset = u32(p);
        r.fileNameOffset = u32(p + 4);
        p += 8;
        if (df.version_ >= 8) {
            r.teamOffset = u32(p);
            p += 4;
        }
        r.structIndex = u32(p);
        std::memcpy(r.id.data(), p + 4, 16);
        r.variantIndex = u16(p + 20);
        r.recordSize = u16(p + 22);
    }

    // Instance data: struct i's instances follow those of every earlier
    // mapping. unforge keys this by the mapping's struct index but takes the
    // record size from the struct at the mapping's own position.
    if (df.mappings_.size() > df.structs_.size())
        return fail(Errc::Format, "DataForge has more data mappings than structs");
    df.structDataOffsets_.assign(df.structs_.size(), -1);
    std::uint64_t dataSize = 0;
    for (std::size_t i = 0; i < df.mappings_.size(); ++i) {
        const DataMapping &m = df.mappings_[i];
        if (m.structIndex < df.structDataOffsets_.size() && df.structDataOffsets_[m.structIndex] < 0)
            df.structDataOffsets_[m.structIndex] = static_cast<std::int64_t>(dataSize);
        dataSize += std::uint64_t(m.structCount) * df.structs_[i].recordSize;
    }
    if (df.dataOffset_ + dataSize > size)
        return fail(Errc::Format, "DataForge instance data runs past the end of the file");

    // Names.
    df.structNames_.resize(df.structs_.size());
    df.structNameValid_.resize(df.structs_.size());
    for (std::size_t i = 0; i < df.structs_.size(); ++i) {
        const auto name = df.blob(df.structs_[i].nameOffset).value_or(std::string_view());
        df.structNames_[i] = toUtf8(name);
        df.structNameValid_[i] = !xml::dotNetNameError(name).has_value();
    }
    df.propertyNames_.resize(df.properties_.size());
    df.propertyNameValid_.resize(df.properties_.size());
    for (std::size_t i = 0; i < df.properties_.size(); ++i) {
        const auto name = df.blob(df.properties_[i].nameOffset).value_or(std::string_view());
        df.propertyNames_[i] = toUtf8(name);
        df.propertyNameValid_[i] = !xml::dotNetNameError(name).has_value();
    }

    // Flattened property lists: the root ancestor's properties first.
    df.structProperties_.resize(df.structs_.size());
    for (std::size_t i = 0; i < df.structs_.size(); ++i) {
        std::vector<std::uint32_t> chain;
        for (std::uint32_t s = static_cast<std::uint32_t>(i); s != 0xFFFFFFFFu;
             s = df.structs_[s].parentIndex) {
            if (s >= df.structs_.size() || chain.size() > df.structs_.size())
                return fail(Errc::Format,
                            "DataForge struct hierarchy is broken at struct " + std::to_string(i));
            chain.push_back(s);
        }
        auto &props = df.structProperties_[i];
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const StructDef &s = df.structs_[*it];
            for (std::uint32_t p = s.firstProperty; p < std::uint32_t(s.firstProperty) + s.propertyCount;
                 ++p) {
                if (p >= df.properties_.size())
                    return fail(Errc::Format, "DataForge struct " + df.structNames_[*it] +
                                                  " references a missing property");
                props.push_back(p);
            }
        }
    }

    // Path and GUID maps, last record wins.
    df.byPath_.reserve(df.records_.size());
    df.byGuid_.reserve(df.records_.size());
    std::vector<std::uint32_t> firstSeen;
    firstSeen.reserve(df.records_.size());
    for (std::uint32_t i = 0; i < df.records_.size(); ++i) {
        const auto path = df.recordFileName(i);
        auto [it, inserted] = df.byPath_.try_emplace(path, i);
        if (inserted)
            firstSeen.push_back(i);
        else
            it->second = i;
        df.byGuid_[df.records_[i].id] = i;
    }
    df.fileRecords_.reserve(firstSeen.size());
    for (const std::uint32_t first : firstSeen)
        df.fileRecords_.push_back(df.byPath_.at(df.recordFileName(first)));

    return df;
}

std::optional<std::string_view> DataForge::text(std::uint64_t offset) const
{
    return cStringAt(data_, textOffset_, textLength_, offset);
}

std::optional<std::string_view> DataForge::blob(std::uint64_t offset) const
{
    if (version_ < 6)
        return text(offset);
    return cStringAt(data_, blobOffset_, blobLength_, offset);
}

std::string_view DataForge::recordName(std::uint32_t index) const
{
    return blob(records_[index].nameOffset).value_or(std::string_view());
}

std::string_view DataForge::recordFileName(std::uint32_t index) const
{
    return text(records_[index].fileNameOffset).value_or(std::string_view());
}

std::string_view DataForge::recordTeam(std::uint32_t index) const
{
    return blob(records_[index].teamOffset).value_or(std::string_view());
}

std::optional<std::uint64_t> DataForge::instanceOffset(std::uint32_t structIndex, std::uint32_t variant) const
{
    if (structIndex >= structDataOffsets_.size() || structDataOffsets_[structIndex] < 0)
        return std::nullopt;
    return dataOffset_ + static_cast<std::uint64_t>(structDataOffsets_[structIndex]) +
           std::uint64_t(structs_[structIndex].recordSize) * variant;
}

const std::uint8_t *DataForge::poolEntry(Pool pool, std::uint64_t index) const
{
    const int p = static_cast<int>(pool);
    if (index >= poolCounts_[p])
        return nullptr;
    return data_.data() + poolOffsets_[p] + index * kPoolEntrySize[p];
}

std::optional<std::uint32_t> DataForge::recordByPath(std::string_view path) const
{
    if (auto it = byPath_.find(path); it != byPath_.end())
        return it->second;
    return std::nullopt;
}

std::optional<std::uint32_t> DataForge::recordByGuid(GuidBytes id) const
{
    if (auto it = byGuid_.find(id); it != byGuid_.end())
        return it->second;
    return std::nullopt;
}

} // namespace engine::forge
