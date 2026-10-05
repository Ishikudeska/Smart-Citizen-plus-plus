#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pugi {
class xml_node;
}

namespace engine::xml {

// Small arena DOM with System.Xml.XmlDocument's append semantics, so ports of
// .NET code that build XML node by node produce the same document.
//
// Nodes are created detached and appended later; a node that is never
// appended is simply never written. Strings are copied into the tree.
// string_views returned by accessors are invalidated by the next mutation.
class XmlTree
{
public:
    using NodeId = std::uint32_t;
    using AttrId = std::uint32_t;
    static constexpr std::uint32_t kNone = 0xFFFFFFFFu;

    enum class Kind : std::uint8_t { Element, CData };

    void clear();

    NodeId createElement(std::string_view name);
    NodeId createCData(std::string_view text);
    void appendChild(NodeId parent, NodeId child);

    // XmlAttributeCollection.Append: an existing attribute with the same name
    // is removed first, so the new one always ends up last.
    void appendAttribute(NodeId element, std::string_view name, std::string_view value);
    // XmlElement.SetAttribute: an existing value is replaced in place.
    void setAttribute(NodeId element, std::string_view name, std::string_view value);

    bool hasContent(NodeId node) const
    {
        return nodes_[node].firstChild != kNone || nodes_[node].firstAttr != kNone;
    }

    Kind kind(NodeId node) const { return nodes_[node].kind; }
    std::string_view name(NodeId node) const { return view(nodes_[node].name); }
    std::string_view text(NodeId node) const { return view(nodes_[node].text); }
    NodeId firstChild(NodeId node) const { return nodes_[node].firstChild; }
    NodeId nextSibling(NodeId node) const { return nodes_[node].nextSibling; }

    AttrId firstAttribute(NodeId node) const { return nodes_[node].firstAttr; }
    AttrId nextAttribute(AttrId attr) const { return attrs_[attr].next; }
    std::string_view attributeName(AttrId attr) const { return view(attrs_[attr].name); }
    std::string_view attributeValue(AttrId attr) const { return view(attrs_[attr].value); }
    std::optional<std::string_view> attribute(NodeId node, std::string_view name) const;

    std::size_t nodeCount() const { return nodes_.size(); }

    // Copies `node` and its subtree under `parent` (e.g. a pugi::xml_document).
    void copyTo(NodeId node, pugi::xml_node parent) const;

    // Copies a pugixml element and its subtree into this tree (elements,
    // attributes and CDATA/text content) and returns the new node, detached.
    NodeId copyFrom(pugi::xml_node node);

private:
    struct Str
    {
        std::uint32_t offset = 0;
        std::uint32_t length = 0;
    };
    struct Node
    {
        Str name;
        Str text;
        NodeId firstChild = kNone;
        NodeId lastChild = kNone;
        NodeId nextSibling = kNone;
        AttrId firstAttr = kNone;
        AttrId lastAttr = kNone;
        Kind kind = Kind::Element;
    };
    struct Attr
    {
        Str name;
        Str value;
        AttrId next = kNone;
    };

    Str store(std::string_view s);
    std::string_view view(Str s) const { return std::string_view(pool_.data() + s.offset, s.length); }
    AttrId findAttribute(NodeId element, std::string_view name, AttrId *previous) const;

    std::vector<Node> nodes_;
    std::vector<Attr> attrs_;
    std::string pool_;
};

} // namespace engine::xml
