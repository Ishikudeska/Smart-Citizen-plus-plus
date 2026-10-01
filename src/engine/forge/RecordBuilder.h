#pragma once

#include "engine/forge/DataForge.h"
#include "engine/xml/XmlTree.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace engine::forge {

struct BuildOptions
{
    int maxPointerDepth = 100;  // unforge's MaxPointerDepth
    int maxReferenceDepth = 1;  // unforge's MaxReferenceDepth: references stay GUID strings
    int maxNodes = 10000;       // children per struct and elements per array
    // The unforge build Smart Citizen bundles throws on null pointers inside
    // arrays and writes the .NET exception text as an element. On (default)
    // reproduces that, so the cache matches it byte for byte. Off skips null
    // entries, as upstream unp4k does since.
    bool nullArrayErrors = true;
};

// Turns one DataForge record into an XML tree the way unforge's
// ReadRecordByPathAsXml does: property order, pruning of empty nodes, pointer
// and reference handling, depth limits and error text. Not thread-safe; use
// one builder per thread over a shared DataForge.
class RecordBuilder
{
public:
    explicit RecordBuilder(const DataForge &forge, BuildOptions options = {});

    // Builds record `index` into `tree` (cleared first). Returns the root
    // element, or XmlTree::kNone when the record renders empty and unforge
    // would skip it.
    xml::XmlTree::NodeId build(std::uint32_t index, xml::XmlTree &tree);

private:
    enum class ChildKind { None, Attribute, Element };
    struct Child
    {
        ChildKind kind = ChildKind::None;
        xml::XmlTree::NodeId element = xml::XmlTree::kNone;
    };

    using NodeId = xml::XmlTree::NodeId;

    NodeId readRecordAtIndex(std::uint32_t index);
    NodeId recordAsXml(std::uint32_t index);
    NodeId readRecordByReference(const std::uint8_t *guid);
    NodeId readStructAtIndex(NodeId node, std::uint32_t structIndex, std::uint32_t variant);
    NodeId readStructAs(NodeId node, std::uint32_t structIndex);
    template <class Consume>
    void structChildren(NodeId target, std::uint32_t structIndex, Consume &&consume);
    Child readValue(NodeId target, std::uint32_t property);
    void readArray(NodeId array, std::uint32_t property);
    NodeId readArrayValue(std::uint32_t property, std::uint32_t firstIndex, std::uint16_t offset);

    const std::uint8_t *take(std::size_t n);
    const std::uint8_t *poolEntry(Pool pool, std::uint32_t index) const;
    std::string_view textAt(std::uint32_t offset) const;

    NodeId newPropertyElement(std::uint32_t property);
    NodeId newStructElement(std::uint32_t structIndex);
    NodeId newLatin1Element(std::string_view latin1);
    NodeId withValue(NodeId element, std::string_view value);
    Child appendPropertyAttribute(NodeId target, std::uint32_t property, std::string_view value);

    bool followReferences() const { return options_.maxReferenceDepth > static_cast<int>(structStack_.size()); }
    bool followStrongPointers() const { return options_.maxPointerDepth > static_cast<int>(recordStack_.size()); }

    const DataForge &forge_;
    BuildOptions options_;
    xml::XmlTree *tree_ = nullptr;
    std::uint64_t pos_ = 0;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> structStack_;
    std::vector<std::uint32_t> recordStack_;
    std::string scratch_;
};

// The bytes unforge writes for record `index`, or false if it skips it.
bool writeRecordXml(RecordBuilder &builder, xml::XmlTree &tree, std::uint32_t index, std::string &out);

} // namespace engine::forge
