#pragma once

#include "engine/Error.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace engine::forge {

enum class DataType : std::uint16_t {
    Boolean = 0x0001,
    Int8 = 0x0002,
    Int16 = 0x0003,
    Int32 = 0x0004,
    Int64 = 0x0005,
    UInt8 = 0x0006,
    UInt16 = 0x0007,
    UInt32 = 0x0008,
    UInt64 = 0x0009,
    String = 0x000A,
    Single = 0x000B,
    Double = 0x000C,
    Locale = 0x000D,
    Guid = 0x000E,
    Enum = 0x000F,
    Class = 0x0010,
    StrongPointer = 0x0110,
    WeakPointer = 0x0210,
    Reference = 0x0310,
};

// unforge's EDataType name ("varStrongPointer"), or the number if unknown.
std::string dataTypeName(std::uint16_t type);

enum class Conversion : std::uint8_t {
    Attribute = 0,
    ComplexArray = 1,
    SimpleArray = 2,
    ClassArray = 3,
};

using GuidBytes = std::array<std::uint8_t, 16>;

struct StructDef
{
    std::uint32_t nameOffset = 0;
    std::uint32_t parentIndex = 0xFFFFFFFFu;
    std::uint16_t propertyCount = 0;
    std::uint16_t firstProperty = 0;
    std::uint32_t recordSize = 0;
};

struct PropertyDef
{
    std::uint32_t nameOffset = 0;
    std::uint16_t index = 0; // struct index for Class properties
    std::uint16_t dataType = 0;
    std::uint8_t conversion = 0;
    std::uint16_t variantIndex = 0;
};

struct DataMapping
{
    std::uint32_t structCount = 0;
    std::uint32_t structIndex = 0;
};

struct RecordDef
{
    std::uint32_t nameOffset = 0;
    std::uint32_t fileNameOffset = 0;
    std::uint32_t teamOffset = 0; // version 8+
    std::uint32_t structIndex = 0;
    GuidBytes id{};
    std::uint16_t variantIndex = 0;
    std::uint16_t recordSize = 0;
};

// Value pools, in file order.
enum class Pool : std::uint8_t {
    Int8,
    Int16,
    Int32,
    Int64,
    UInt8,
    UInt16,
    UInt32,
    UInt64,
    Boolean,
    Single,
    Double,
    Guid,
    String,
    Locale,
    Enum,
    StrongPointer,
    WeakPointer,
    Reference,
    EnumOption,
    Count,
};

struct GuidHash
{
    std::size_t operator()(const GuidBytes &g) const noexcept;
};

// A DataForge database (Game2.dcb), held in memory and read in place.
// Ports unforge's DataForge: non-legacy files, version 5 and later (LIVE is
// version 8). Immutable after load, so it can be shared across threads.
class DataForge
{
public:
    static Result<DataForge> load(std::vector<std::uint8_t> bytes);

    int version() const { return version_; }
    std::span<const std::uint8_t> bytes() const { return data_; }

    const std::vector<StructDef> &structs() const { return structs_; }
    const std::vector<PropertyDef> &properties() const { return properties_; }
    const std::vector<DataMapping> &mappings() const { return mappings_; }
    const std::vector<RecordDef> &records() const { return records_; }

    // Latin-1 C-strings from the text and blob tables. Empty optional when
    // the offset is past the table, where unforge throws.
    std::optional<std::string_view> text(std::uint64_t offset) const;
    std::optional<std::string_view> blob(std::uint64_t offset) const;
    std::uint64_t textLength() const { return textLength_; }

    // Struct and property names, converted to UTF-8 at load, and whether
    // System.Xml accepts them as element/attribute names.
    const std::string &structName(std::uint32_t index) const { return structNames_[index]; }
    const std::string &propertyName(std::uint32_t index) const { return propertyNames_[index]; }
    bool structNameValid(std::uint32_t index) const { return structNameValid_[index]; }
    bool propertyNameValid(std::uint32_t index) const { return propertyNameValid_[index]; }

    std::string_view recordName(std::uint32_t index) const;     // Latin-1
    std::string_view recordFileName(std::uint32_t index) const; // Latin-1, e.g. libs/foundry/records/...
    std::string_view recordTeam(std::uint32_t index) const;     // Latin-1

    // Properties of a struct including inherited ones, root ancestor first.
    const std::vector<std::uint32_t> &structProperties(std::uint32_t structIndex) const
    {
        return structProperties_[structIndex];
    }

    // Offset into bytes() of instance `variant` of a struct, or nothing if
    // the struct has no data mapping.
    std::optional<std::uint64_t> instanceOffset(std::uint32_t structIndex, std::uint32_t variant) const;

    std::uint64_t poolCount(Pool pool) const { return poolCounts_[static_cast<int>(pool)]; }
    // Pointer to entry `index` of a value pool, or nullptr if out of range.
    const std::uint8_t *poolEntry(Pool pool, std::uint64_t index) const;

    // unforge's maps: the last record with a given path or GUID wins.
    std::optional<std::uint32_t> recordByPath(std::string_view path) const;
    std::optional<std::uint32_t> recordByGuid(const GuidBytes &id) const;

    // The record unforge writes for each distinct file path, in order of the
    // path's first appearance.
    const std::vector<std::uint32_t> &fileRecords() const { return fileRecords_; }

private:
    std::vector<std::uint8_t> data_;
    int version_ = 0;

    std::vector<StructDef> structs_;
    std::vector<PropertyDef> properties_;
    std::vector<DataMapping> mappings_;
    std::vector<RecordDef> records_;

    std::array<std::uint64_t, static_cast<int>(Pool::Count)> poolOffsets_{};
    std::array<std::uint64_t, static_cast<int>(Pool::Count)> poolCounts_{};
    std::uint64_t textOffset_ = 0;
    std::uint64_t textLength_ = 0;
    std::uint64_t blobOffset_ = 0;
    std::uint64_t blobLength_ = 0;
    std::uint64_t dataOffset_ = 0;

    std::vector<std::int64_t> structDataOffsets_; // relative to dataOffset_, -1 if unmapped
    std::vector<std::vector<std::uint32_t>> structProperties_;
    std::vector<std::string> structNames_;
    std::vector<std::string> propertyNames_;
    std::vector<bool> structNameValid_;
    std::vector<bool> propertyNameValid_;

    std::unordered_map<std::string_view, std::uint32_t> byPath_;
    std::unordered_map<GuidBytes, std::uint32_t, GuidHash> byGuid_;
    std::vector<std::uint32_t> fileRecords_;
};

} // namespace engine::forge
