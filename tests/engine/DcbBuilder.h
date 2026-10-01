#pragma once

// Builds small DataForge (.dcb, version 8) files for tests.

#include "engine/forge/DataForge.h"

#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace testing {

using engine::forge::Conversion;
using engine::forge::DataType;
using engine::forge::GuidBytes;
using engine::forge::Pool;

// Bytes of one struct instance, appended in property order.
struct Inst
{
    std::vector<std::uint8_t> b;

    template <class T>
    Inst &raw(T v)
    {
        const auto *p = reinterpret_cast<const std::uint8_t *>(&v);
        b.insert(b.end(), p, p + sizeof v);
        return *this;
    }
    Inst &u8(std::uint8_t v) { return raw(v); }
    Inst &i8(std::int8_t v) { return raw(v); }
    Inst &u16(std::uint16_t v) { return raw(v); }
    Inst &i16(std::int16_t v) { return raw(v); }
    Inst &u32(std::uint32_t v) { return raw(v); }
    Inst &i32(std::int32_t v) { return raw(v); }
    Inst &u64(std::uint64_t v) { return raw(v); }
    Inst &f32(float v) { return raw(v); }
    Inst &f64(double v) { return raw(v); }
    Inst &guid(const GuidBytes &g)
    {
        b.insert(b.end(), g.begin(), g.end());
        return *this;
    }
    Inst &ptr(std::uint32_t structIndex, std::uint16_t variant) { return u32(structIndex).u16(variant).u16(0); }
    Inst &nullPtr() { return ptr(0xFFFFFFFFu, 0xFFFF); }
    Inst &ref(const GuidBytes &g) { return u32(0).guid(g); }
    Inst &array(std::uint32_t count, std::uint32_t first) { return u32(count).u32(first); }
    Inst &append(const Inst &other)
    {
        b.insert(b.end(), other.b.begin(), other.b.end());
        return *this;
    }
};

inline GuidBytes makeGuid(std::uint8_t seed)
{
    GuidBytes g{};
    for (int i = 0; i < 16; ++i)
        g[i] = static_cast<std::uint8_t>(seed + i);
    return g;
}

class DcbBuilder
{
public:
    static constexpr std::uint32_t kNoParent = 0xFFFFFFFFu;

    std::uint32_t addStruct(const std::string &name, std::uint32_t parent = kNoParent)
    {
        structs_.push_back({name, parent, {}, {}});
        return static_cast<std::uint32_t>(structs_.size() - 1);
    }

    void addProperty(std::uint32_t owner, const std::string &name, DataType type,
                     Conversion conversion = Conversion::Attribute, std::uint16_t index = 0)
    {
        structs_[owner].properties.push_back({name, static_cast<std::uint16_t>(type),
                                              static_cast<std::uint8_t>(conversion), index});
    }

    // Returns the instance's variant index.
    std::uint16_t addInstance(std::uint32_t structIndex, const Inst &inst)
    {
        structs_[structIndex].instances.push_back(inst.b);
        return static_cast<std::uint16_t>(structs_[structIndex].instances.size() - 1);
    }

    void addRecord(const std::string &name, const std::string &path, std::uint32_t structIndex,
                   std::uint16_t variant, const GuidBytes &id, const std::string &team = "Unknown")
    {
        records_.push_back({name, path, team, structIndex, variant, id});
    }

    std::uint32_t text(const std::string &s) { return intern(text_, textOffsets_, s); }

    // Appends pool entries and returns the index of the first.
    std::uint32_t addPool(Pool pool, const std::vector<Inst> &entries)
    {
        auto &p = pools_[static_cast<int>(pool)];
        const std::uint32_t first = p.count;
        for (const auto &e : entries) {
            p.bytes.insert(p.bytes.end(), e.b.begin(), e.b.end());
            ++p.count;
        }
        return first;
    }

    std::vector<std::uint8_t> build() const
    {
        std::string blob(1, '\0');
        std::map<std::string, std::uint32_t> blobOffsets{{"", 0}};
        std::string text = text_;

        // Properties laid out struct by struct.
        struct Prop
        {
            std::uint32_t nameOffset;
            std::uint16_t index, type;
            std::uint8_t conversion;
        };
        std::vector<Prop> props;
        std::vector<std::uint16_t> firstProp;
        for (const auto &s : structs_) {
            firstProp.push_back(static_cast<std::uint16_t>(props.size()));
            for (const auto &p : s.properties)
                props.push_back({intern(blob, blobOffsets, p.name), p.index, p.type, p.conversion});
        }

        std::vector<std::uint8_t> out;
        auto put16 = [&](std::uint16_t v) {
            out.push_back(static_cast<std::uint8_t>(v));
            out.push_back(static_cast<std::uint8_t>(v >> 8));
        };
        auto put32 = [&](std::uint32_t v) {
            for (int i = 0; i < 4; ++i)
                out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        };

        // Record names, paths and teams go into the string tables first so the
        // header can carry their final lengths.
        struct Rec
        {
            std::uint32_t name, path, team;
        };
        std::vector<Rec> recs;
        std::map<std::string, std::uint32_t> textOffsets = textOffsets_;
        for (const auto &r : records_)
            recs.push_back({intern(blob, blobOffsets, r.name), intern(text, textOffsets, r.path),
                            intern(blob, blobOffsets, r.team)});
        std::vector<std::uint32_t> structNames;
        for (const auto &s : structs_)
            structNames.push_back(intern(blob, blobOffsets, s.name));

        // Header.
        put16(0);
        put16(0);
        put32(8);
        for (int i = 0; i < 4; ++i)
            put16(0);
        const auto poolCount = [&](Pool p) { return pools_[static_cast<int>(p)].count; };
        const std::uint32_t counts[24] = {
            static_cast<std::uint32_t>(structs_.size()), static_cast<std::uint32_t>(props.size()), 0,
            static_cast<std::uint32_t>(structs_.size()), static_cast<std::uint32_t>(records_.size()),
            poolCount(Pool::Boolean), poolCount(Pool::Int8), poolCount(Pool::Int16), poolCount(Pool::Int32),
            poolCount(Pool::Int64), poolCount(Pool::UInt8), poolCount(Pool::UInt16), poolCount(Pool::UInt32),
            poolCount(Pool::UInt64), poolCount(Pool::Single), poolCount(Pool::Double), poolCount(Pool::Guid),
            poolCount(Pool::String), poolCount(Pool::Locale), poolCount(Pool::Enum),
            poolCount(Pool::StrongPointer), poolCount(Pool::WeakPointer), poolCount(Pool::Reference),
            poolCount(Pool::EnumOption)};
        for (std::uint32_t c : counts)
            put32(c);
        put32(static_cast<std::uint32_t>(text.size()));
        put32(static_cast<std::uint32_t>(blob.size()));

        // Struct definitions, with record sizes from their flattened properties.
        for (std::size_t i = 0; i < structs_.size(); ++i) {
            put32(structNames[i]);
            put32(structs_[i].parent);
            put16(static_cast<std::uint16_t>(structs_[i].properties.size()));
            put16(firstProp[i]);
            put32(recordSize(static_cast<std::uint32_t>(i)));
        }
        for (const auto &p : props) {
            put32(p.nameOffset);
            put16(p.index);
            put16(p.type);
            put16(p.conversion);
            put16(0);
        }
        for (std::size_t i = 0; i < structs_.size(); ++i) {
            put32(static_cast<std::uint32_t>(structs_[i].instances.size()));
            put32(static_cast<std::uint32_t>(i));
        }
        for (std::size_t i = 0; i < records_.size(); ++i) {
            put32(recs[i].name);
            put32(recs[i].path);
            put32(recs[i].team);
            put32(records_[i].structIndex);
            out.insert(out.end(), records_[i].id.begin(), records_[i].id.end());
            put16(records_[i].variant);
            put16(0);
        }
        for (const auto &p : pools_)
            out.insert(out.end(), p.bytes.begin(), p.bytes.end());
        out.insert(out.end(), text.begin(), text.end());
        out.insert(out.end(), blob.begin(), blob.end());
        for (std::size_t i = 0; i < structs_.size(); ++i)
            for (const auto &inst : structs_[i].instances) {
                if (inst.size() != recordSize(static_cast<std::uint32_t>(i)))
                    throw std::logic_error("instance of " + structs_[i].name + " has the wrong size");
                out.insert(out.end(), inst.begin(), inst.end());
            }
        return out;
    }

    std::uint32_t recordSize(std::uint32_t structIndex) const
    {
        std::uint32_t size = 0;
        const auto &s = structs_[structIndex];
        if (s.parent != kNoParent)
            size += recordSize(s.parent);
        for (const auto &p : s.properties) {
            if (p.conversion != static_cast<std::uint8_t>(Conversion::Attribute)) {
                size += 8;
                continue;
            }
            switch (static_cast<DataType>(p.type)) {
            case DataType::Boolean:
            case DataType::Int8:
            case DataType::UInt8: size += 1; break;
            case DataType::Int16:
            case DataType::UInt16: size += 2; break;
            case DataType::Int32:
            case DataType::UInt32:
            case DataType::Single:
            case DataType::String:
            case DataType::Locale:
            case DataType::Enum: size += 4; break;
            case DataType::Int64:
            case DataType::UInt64:
            case DataType::Double:
            case DataType::StrongPointer:
            case DataType::WeakPointer: size += 8; break;
            case DataType::Guid: size += 16; break;
            case DataType::Reference: size += 20; break;
            case DataType::Class: size += recordSize(p.index); break;
            }
        }
        return size;
    }

private:
    static std::uint32_t intern(std::string &table, std::map<std::string, std::uint32_t> &offsets,
                                const std::string &s)
    {
        if (auto it = offsets.find(s); it != offsets.end())
            return it->second;
        const auto off = static_cast<std::uint32_t>(table.size());
        table += s;
        table.push_back('\0');
        offsets[s] = off;
        return off;
    }

    struct PropDef
    {
        std::string name;
        std::uint16_t type;
        std::uint8_t conversion;
        std::uint16_t index;
    };
    struct StructDef
    {
        std::string name;
        std::uint32_t parent;
        std::vector<PropDef> properties;
        std::vector<std::vector<std::uint8_t>> instances;
    };
    struct RecordDef
    {
        std::string name, path, team;
        std::uint32_t structIndex;
        std::uint16_t variant;
        GuidBytes id;
    };
    struct PoolData
    {
        std::vector<std::uint8_t> bytes;
        std::uint32_t count = 0;
    };

    std::vector<StructDef> structs_;
    std::vector<RecordDef> records_;
    std::string text_ = std::string(1, '\0');
    std::map<std::string, std::uint32_t> textOffsets_{{"", 0}};
    PoolData pools_[static_cast<int>(Pool::Count)];
};

} // namespace testing
