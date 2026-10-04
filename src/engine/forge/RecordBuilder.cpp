#include "engine/forge/RecordBuilder.h"

#include "engine/forge/DotNetFormat.h"
#include "engine/xml/DotNetXmlWriter.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace engine::forge {

namespace {

using NodeId = xml::XmlTree::NodeId;
constexpr NodeId kNone = xml::XmlTree::kNone;

// A .NET exception in flight. unforge catches these at three levels and
// writes either ToString() (type, message, stack) or just the message.
struct DotNetException
{
    std::string type;
    std::string message;
    std::string stack;

    std::string toString() const
    {
        std::string s = type + ": " + message;
        if (!stack.empty()) {
            s += "\r\n";
            s += stack;
        }
        return s;
    }
};

// The stack .NET reports for a bad index inside ReadArrayValueAsXml. It is
// the only exception the LIVE data raises (a null pointer inside an array),
// so this text is what lands in the cache. In 69 of ~27k cases the JIT had
// not yet inlined ReadDataMappingAtIndex and an extra frame appears; that is
// nondeterministic, so the single-frame form is the one reproduced.
constexpr const char *kArrayFrame =
    "   at unforge.DataForgeStructDefinition.ReadArrayValueAsXml(XmlNode parentNode, DataForgePropertyDefinition "
    "propertyDefinition, UInt32 firstIndex, UInt16 offset)";
constexpr const char *kValueFrame = "   at unforge.DataForgeStructDefinition.ReadValueAsXml(XmlNode parentNode, "
                                    "DataForgePropertyDefinition propertyDefinition, String nameOverride)";
constexpr const char *kStructFrame =
    "   at unforge.DataForge.ReadStructAtIndexAsXml(XmlElement xmlNode, UInt32 structIndex, UInt32 variantIndex)";

[[noreturn]] void throwIndexOutOfRange(const char *frame,
                                       std::string message = "Index was outside the bounds of the array.")
{
    throw DotNetException{"System.IndexOutOfRangeException", std::move(message), frame};
}

[[noreturn]] void throwNameError(const std::string &message)
{
    if (message.starts_with("The local name"))
        throw DotNetException{"System.ArgumentException", message, ""};
    throw DotNetException{"System.Xml.XmlException", message, ""};
}

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

bool isNullGuid(const std::uint8_t *g)
{
    for (int i = 0; i < 16; ++i)
        if (g[i] != 0)
            return false;
    return true;
}

// "{StructName}[{variant:X4}]", what unforge writes for weak pointers.
std::string pointerText(const std::string &structName, std::uint16_t variant)
{
    char hex[8];
    std::snprintf(hex, sizeof hex, "%04X", variant);
    return structName + "[" + hex + "]";
}

template <class F>
struct ScopeExit
{
    explicit ScopeExit(F fn) : f(std::move(fn)) {}
    ScopeExit(const ScopeExit &) = delete;
    ScopeExit &operator=(const ScopeExit &) = delete;
    ~ScopeExit() { f(); }
    F f;
};
template <class F>
ScopeExit(F) -> ScopeExit<F>;

} // namespace

RecordBuilder::RecordBuilder(const DataForge &forge, BuildOptions options)
    : forge_(forge)
    , options_(options)
{
}

NodeId RecordBuilder::build(std::uint32_t index, xml::XmlTree &tree)
{
    tree.clear();
    tree_ = &tree;
    pos_ = 0;
    structStack_.clear();
    recordStack_.clear();
    return readRecordAtIndex(index);
}

// --- Primitive reads -------------------------------------------------------

const std::uint8_t *RecordBuilder::take(std::size_t n)
{
    const auto bytes = forge_.bytes();
    if (pos_ > bytes.size() || n > bytes.size() - pos_)
        throw DotNetException{"System.IO.EndOfStreamException", "Unable to read beyond the end of the stream.", ""};
    const std::uint8_t *p = bytes.data() + pos_;
    pos_ += n;
    return p;
}

const std::uint8_t *RecordBuilder::poolEntry(Pool pool, std::uint32_t index) const
{
    const std::uint8_t *p = forge_.poolEntry(pool, index);
    if (!p)
        throwIndexOutOfRange(kArrayFrame);
    return p;
}

std::string_view RecordBuilder::textAt(std::uint32_t offset) const
{
    const auto text = forge_.text(offset);
    if (!text) {
        // ReadTextAtOffset's own message; its stack can't be reproduced.
        throwIndexOutOfRange("   at unforge.DataForge.ReadTextAtOffset(Int64 offset)",
                             "Offset " + std::to_string(offset) + " is out of range for Text values (length: " +
                                 std::to_string(forge_.textLength()) + ")");
    }
    return *text;
}

// --- Node creation (XmlDocument validates names) ---------------------------

NodeId RecordBuilder::newPropertyElement(std::uint32_t property)
{
    if (!forge_.propertyNameValid(property))
        throwNameError(*xml::dotNetNameError(*forge_.blob(forge_.properties()[property].nameOffset)));
    return tree_->createElement(forge_.propertyName(property));
}

NodeId RecordBuilder::newStructElement(std::uint32_t structIndex)
{
    if (!forge_.structNameValid(structIndex))
        throwNameError(*xml::dotNetNameError(*forge_.blob(forge_.structs()[structIndex].nameOffset)));
    return tree_->createElement(forge_.structName(structIndex));
}

NodeId RecordBuilder::newLatin1Element(std::string_view latin1)
{
    if (auto error = xml::dotNetNameError(latin1))
        throwNameError(*error);
    scratch_.clear();
    xml::appendLatin1AsUtf8(scratch_, latin1);
    return tree_->createElement(scratch_);
}

// Extensions.CreateElementWithValue: adds value="..." and returns null when
// the value is empty.
NodeId RecordBuilder::withValue(NodeId element, std::string_view value)
{
    tree_->appendAttribute(element, "value", value);
    return value.empty() ? kNone : element;
}

// Extensions.CreateAttributeWithValue, appended straight to its parent: the
// .NET code appends each attribute as soon as it is produced, so this is the
// same order.
RecordBuilder::Child RecordBuilder::appendPropertyAttribute(NodeId target, std::uint32_t property,
                                                            std::string_view value)
{
    if (!forge_.propertyNameValid(property))
        throwNameError(*xml::dotNetNameError(*forge_.blob(forge_.properties()[property].nameOffset)));
    if (value.empty())
        return {};
    tree_->appendAttribute(target, forge_.propertyName(property), value);
    return {ChildKind::Attribute};
}

// --- Records --------------------------------------------------------------

// DataForge.ReadRecordAtIndexAsXml
NodeId RecordBuilder::readRecordAtIndex(std::uint32_t index)
{
    const std::uint64_t saved = pos_;
    if (recordStack_.size() > static_cast<std::size_t>(options_.maxReferenceDepth) ||
        std::find(recordStack_.begin(), recordStack_.end(), index) != recordStack_.end())
        return kNone;

    recordStack_.push_back(index);
    NodeId result = kNone;
    try {
        result = recordAsXml(index);
    } catch (const DotNetException &ex) {
        result = withValue(tree_->createElement("Error"), ex.message);
    }
    pos_ = saved;
    recordStack_.pop_back();
    return result;
}

// DataForgeRecordDefinition.ReadAsXml
NodeId RecordBuilder::recordAsXml(std::uint32_t index)
{
    const RecordDef &record = forge_.records()[index];
    const NodeId element = newLatin1Element(forge_.recordName(index));
    const NodeId root = readStructAtIndex(element, record.structIndex, record.variantIndex);
    if (root == kNone)
        return kNone;

    tree_->appendAttribute(root, "__type", forge_.structName(record.structIndex));
    scratch_.clear();
    appendGuid(scratch_, record.id.data());
    tree_->appendAttribute(root, "__ref", scratch_);
    scratch_.clear();
    xml::appendLatin1AsUtf8(scratch_, forge_.recordFileName(index));
    tree_->appendAttribute(root, "__path", scratch_);
    if (forge_.version() >= 8) {
        scratch_.clear();
        xml::appendLatin1AsUtf8(scratch_, forge_.recordTeam(index));
        tree_->appendAttribute(root, "__team", scratch_);
    }
    return root;
}

NodeId RecordBuilder::readRecordByReference(const std::uint8_t *guid)
{
    GuidBytes id;
    std::memcpy(id.data(), guid, 16);
    const auto index = forge_.recordByGuid(id);
    if (!index)
        throw DotNetException{"System.IO.FileNotFoundException", "Unable to find the specified file.", ""};
    return readRecordAtIndex(*index);
}

// --- Structs --------------------------------------------------------------

// DataForge.ReadStructAtIndexAsXml
NodeId RecordBuilder::readStructAtIndex(NodeId node, std::uint32_t structIndex, std::uint32_t variant)
{
    const std::uint64_t saved = pos_;
    const std::pair key{structIndex, variant};
    if (structStack_.size() > static_cast<std::size_t>(options_.maxPointerDepth) ||
        std::find(structStack_.begin(), structStack_.end(), key) != structStack_.end())
        return kNone;

    structStack_.push_back(key);
    ScopeExit restore{[this, saved] {
        pos_ = saved;
        structStack_.pop_back();
    }};

    if (structIndex >= forge_.structs().size() || structIndex >= forge_.mappings().size())
        throwIndexOutOfRange(kStructFrame);
    const DataMapping &mapping = forge_.mappings()[structIndex];
    if (mapping.structCount < variant)
        throwIndexOutOfRange(kStructFrame, "Variant Index " + std::to_string(variant) +
                                               " is out of range for struct " + forge_.structName(structIndex) +
                                               " with count " + std::to_string(mapping.structCount));
    const auto offset = forge_.instanceOffset(structIndex, variant);
    if (!offset)
        throw DotNetException{"System.Collections.Generic.KeyNotFoundException",
                              "Struct Index " + std::to_string(structIndex) +
                                  " not found in Struct to Data Offset Map",
                              kStructFrame};
    pos_ = *offset;
    return readStructAs(node, structIndex);
}

// DataForge.ReadStructAsXml
NodeId RecordBuilder::readStructAs(NodeId node, std::uint32_t structIndex)
{
    int count = 0;
    structChildren(node, structIndex, [&](Child child) {
        if (child.kind == ChildKind::None)
            return true;
        if (child.kind == ChildKind::Element)
            tree_->appendChild(node, child.element);
        return ++count < options_.maxNodes;
    });
    return tree_->hasContent(node) ? node : kNone;
}

// DataForgeStructDefinition.ReadAsXml, a lazy generator in C#: `consume`
// sees each child as it is produced and can stop the walk.
template <class Consume>
void RecordBuilder::structChildren(NodeId target, std::uint32_t structIndex, Consume &&consume)
{
    int produced = 0;
    for (const std::uint32_t property : forge_.structProperties(structIndex)) {
        Child child;
        if (forge_.properties()[property].conversion == static_cast<std::uint8_t>(Conversion::Attribute)) {
            child = readValue(target, property);
        } else {
            const NodeId array = newPropertyElement(property);
            readArray(array, property);
            if (!tree_->hasContent(array))
                continue; // skips the node count, as the C# `continue` does
            child = {ChildKind::Element, array};
        }
        if (!consume(child))
            return;
        if (++produced >= options_.maxNodes)
            return;
    }
}

// --- Values ---------------------------------------------------------------

// DataForgeStructDefinition.ReadValueAsXml
RecordBuilder::Child RecordBuilder::readValue(NodeId target, std::uint32_t property)
{
    const PropertyDef &prop = forge_.properties()[property];
    try {
        switch (static_cast<DataType>(prop.dataType)) {
        case DataType::Class: {
            if (prop.index >= forge_.structs().size())
                throwIndexOutOfRange(kValueFrame);
            const NodeId element = newPropertyElement(property);
            structChildren(element, prop.index, [&](Child child) {
                if (child.kind == ChildKind::Element)
                    tree_->appendChild(element, child.element);
                return true;
            });
            if (!tree_->hasContent(element))
                return {};
            return {ChildKind::Element, element};
        }
        case DataType::Reference: {
            const std::uint8_t *ref = take(20);
            const std::uint8_t *guid = ref + 4;
            if (isNullGuid(guid))
                return {};
            if (!followReferences()) {
                const std::string text = formatGuid(guid);
                return appendPropertyAttribute(target, property, text);
            }
            const NodeId record = readRecordByReference(guid);
            if (record == kNone) {
                const std::string text = formatGuid(guid);
                return appendPropertyAttribute(target, property, text);
            }
            if (!tree_->hasContent(record))
                return {};
            return {ChildKind::Element, record};
        }
        case DataType::StrongPointer: {
            const std::uint8_t *ptr = take(8);
            const std::uint32_t structIndex = u32(ptr);
            const std::uint16_t variant = u16(ptr + 4);
            if (structIndex == 0xFFFFFFFFu && variant == 0xFFFF)
                return {};
            if (structIndex >= forge_.structs().size())
                throwIndexOutOfRange(kValueFrame);
            if (!followStrongPointers()) {
                const NodeId element = withValue(newPropertyElement(property),
                                                 pointerText(forge_.structName(structIndex), variant));
                return element == kNone ? Child{} : Child{ChildKind::Element, element};
            }
            const NodeId inner = readStructAtIndex(newStructElement(structIndex), structIndex, variant);
            if (inner == kNone || !tree_->hasContent(inner))
                return {};
            const NodeId wrapper = newPropertyElement(property);
            tree_->appendChild(wrapper, inner);
            return {ChildKind::Element, wrapper};
        }
        case DataType::WeakPointer: {
            const std::uint8_t *ptr = take(8);
            const std::uint32_t structIndex = u32(ptr);
            const std::uint16_t variant = u16(ptr + 4);
            if (structIndex == 0xFFFFFFFFu && variant == 0xFFFF)
                return {};
            if (structIndex >= forge_.structs().size())
                throwIndexOutOfRange(kValueFrame);
            const NodeId element =
                withValue(newPropertyElement(property), pointerText(forge_.structName(structIndex), variant));
            return element == kNone ? Child{} : Child{ChildKind::Element, element};
        }
        case DataType::Locale:
        case DataType::String:
        case DataType::Enum: {
            const std::string_view text = textAt(u32(take(4)));
            scratch_.clear();
            xml::appendLatin1AsUtf8(scratch_, text);
            return appendPropertyAttribute(target, property, scratch_);
        }
        default:
            break;
        }

        scratch_.clear();
        switch (static_cast<DataType>(prop.dataType)) {
        case DataType::Boolean:
            scratch_ = std::to_string(*take(1)); // the raw byte, not normalized
            break;
        case DataType::Single: {
            float v;
            std::memcpy(&v, take(4), 4);
            appendSingle(scratch_, v);
            break;
        }
        case DataType::Double: {
            double v;
            std::memcpy(&v, take(8), 8);
            appendDouble(scratch_, v);
            break;
        }
        case DataType::Guid:
            appendGuid(scratch_, take(16));
            break;
        case DataType::Int8:
            scratch_ = std::to_string(static_cast<std::int8_t>(*take(1)));
            break;
        case DataType::Int16:
            scratch_ = std::to_string(static_cast<std::int16_t>(u16(take(2))));
            break;
        case DataType::Int32:
            scratch_ = std::to_string(static_cast<std::int32_t>(u32(take(4))));
            break;
        case DataType::Int64:
            scratch_ = std::to_string(static_cast<std::int64_t>(u64(take(8))));
            break;
        case DataType::UInt8:
            scratch_ = std::to_string(*take(1));
            break;
        case DataType::UInt16:
            scratch_ = std::to_string(u16(take(2)));
            break;
        case DataType::UInt32:
            scratch_ = std::to_string(u32(take(4)));
            break;
        case DataType::UInt64:
            scratch_ = std::to_string(u64(take(8)));
            break;
        default:
            scratch_ = "Unhandled Type " + dataTypeName(prop.dataType);
            break;
        }
        return appendPropertyAttribute(target, property, scratch_);
    } catch (const DotNetException &ex) {
        const std::string text = "Error reading property " + forge_.propertyName(property) + " of type " +
                                 dataTypeName(prop.dataType) + ": " + ex.toString();
        return appendPropertyAttribute(target, property, text);
    }
}

// --- Arrays ---------------------------------------------------------------

// DataForgeStructDefinition.ReadArrayAsXml (and the append loop around it)
void RecordBuilder::readArray(NodeId array, std::uint32_t property)
{
    const std::uint8_t *header = take(8);
    const std::uint32_t count = u32(header);
    const std::uint32_t first = u32(header + 4);
    // The C# loop counter is a UInt16; MaxNodes stops it long before it wraps.
    for (std::uint16_t i = 0; i < count; ++i) {
        const NodeId child = readArrayValue(property, first, i);
        if (child != kNone)
            tree_->appendChild(array, child);
        if (i >= options_.maxNodes - 1)
            break;
    }
}

// DataForgeStructDefinition.ReadArrayValueAsXml
NodeId RecordBuilder::readArrayValue(std::uint32_t property, std::uint32_t first, std::uint16_t offset)
{
    const PropertyDef &prop = forge_.properties()[property];
    const std::uint32_t index = first + offset; // UInt32 arithmetic, as in C#
    try {
        auto simple = [this](const char *name, std::string_view value) {
            return withValue(tree_->createElement(name), value);
        };
        switch (static_cast<DataType>(prop.dataType)) {
        case DataType::Boolean:
            return simple("Bool", *poolEntry(Pool::Boolean, index) != 0 ? "1" : "0");
        case DataType::Single: {
            float v;
            std::memcpy(&v, poolEntry(Pool::Single, index), 4);
            return simple("Single", formatSingle(v));
        }
        case DataType::Double: {
            double v;
            std::memcpy(&v, poolEntry(Pool::Double, index), 8);
            return simple("Double", formatDouble(v));
        }
        case DataType::Guid:
            return simple("Guid", formatGuid(poolEntry(Pool::Guid, index)));
        case DataType::Reference: {
            if (followReferences()) {
                const NodeId record = readRecordByReference(poolEntry(Pool::Reference, index) + 4);
                if (record != kNone)
                    return record;
            }
            return simple("Reference", formatGuid(poolEntry(Pool::Reference, index) + 4));
        }
        case DataType::UInt8:
            return simple("UInt8", std::to_string(*poolEntry(Pool::UInt8, index)));
        case DataType::UInt16:
            return simple("UInt16", std::to_string(u16(poolEntry(Pool::UInt16, index))));
        case DataType::UInt32:
            return simple("UInt32", std::to_string(u32(poolEntry(Pool::UInt32, index))));
        case DataType::UInt64:
            return simple("UInt64", std::to_string(u64(poolEntry(Pool::UInt64, index))));
        case DataType::Int8:
            return simple("Int8", std::to_string(static_cast<std::int8_t>(*poolEntry(Pool::Int8, index))));
        case DataType::Int16:
            return simple("Int16", std::to_string(static_cast<std::int16_t>(u16(poolEntry(Pool::Int16, index)))));
        case DataType::Int32:
            return simple("Int32", std::to_string(static_cast<std::int32_t>(u32(poolEntry(Pool::Int32, index)))));
        case DataType::Int64:
            return simple("Int64", std::to_string(static_cast<std::int64_t>(u64(poolEntry(Pool::Int64, index)))));
        case DataType::String:
        case DataType::Locale:
        case DataType::Enum: {
            const Pool pool = prop.dataType == static_cast<std::uint16_t>(DataType::String)   ? Pool::String
                              : prop.dataType == static_cast<std::uint16_t>(DataType::Locale) ? Pool::Locale
                                                                                               : Pool::Enum;
            const char *name = pool == Pool::String ? "String" : pool == Pool::Locale ? "LocID" : "Enum";
            std::string value;
            xml::appendLatin1AsUtf8(value, textAt(u32(poolEntry(pool, index))));
            return simple(name, value);
        }
        case DataType::WeakPointer:
        case DataType::StrongPointer: {
            const Pool pool = prop.dataType == static_cast<std::uint16_t>(DataType::WeakPointer) ? Pool::WeakPointer
                                                                                                 : Pool::StrongPointer;
            const std::uint8_t *ptr = poolEntry(pool, index);
            const std::uint32_t structIndex = u32(ptr);
            const std::uint16_t variant = u16(ptr + 4);
            if (!options_.nullArrayErrors && structIndex == 0xFFFFFFFFu)
                return kNone;
            // DataMapping.Name: the name of the mapping's struct. A null
            // pointer (0xFFFFFFFF) fails right here in unforge.
            if (structIndex >= forge_.mappings().size() ||
                forge_.mappings()[structIndex].structIndex >= forge_.structs().size())
                throwIndexOutOfRange(kArrayFrame);
            const NodeId element = newStructElement(forge_.mappings()[structIndex].structIndex);
            return readStructAtIndex(element, structIndex, variant);
        }
        case DataType::Class: {
            const std::uint32_t structIndex = prop.index;
            if (structIndex >= forge_.mappings().size() ||
                forge_.mappings()[structIndex].structIndex >= forge_.structs().size())
                throwIndexOutOfRange(kArrayFrame);
            const NodeId element = newStructElement(forge_.mappings()[structIndex].structIndex);
            return readStructAtIndex(element, structIndex, index);
        }
        default:
            break;
        }
    } catch (const DotNetException &ex) {
        const std::string text = "Error reading array property " + forge_.propertyName(property) + " of type " +
                                 dataTypeName(prop.dataType) + ": " + ex.toString();
        return withValue(newPropertyElement(property), text);
    }
    return withValue(newPropertyElement(property), "TBC");
}

bool writeRecordXml(RecordBuilder &builder, xml::XmlTree &tree, std::uint32_t index, std::string &out)
{
    out.clear();
    const auto root = builder.build(index, tree);
    if (root == xml::XmlTree::kNone)
        return false;
    xml::writeDotNet(tree, root, out);
    return true;
}

} // namespace engine::forge
