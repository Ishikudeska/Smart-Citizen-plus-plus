#include "engine/xml/XmlTree.h"

#include <pugixml.hpp>

namespace engine::xml {

void XmlTree::clear()
{
    nodes_.clear();
    attrs_.clear();
    pool_.clear();
}

XmlTree::Str XmlTree::store(std::string_view s)
{
    Str out{static_cast<std::uint32_t>(pool_.size()), static_cast<std::uint32_t>(s.size())};
    pool_.append(s);
    return out;
}

XmlTree::NodeId XmlTree::createElement(std::string_view name)
{
    Node n;
    n.name = store(name);
    n.kind = Kind::Element;
    nodes_.push_back(n);
    return static_cast<NodeId>(nodes_.size() - 1);
}

XmlTree::NodeId XmlTree::createCData(std::string_view text)
{
    Node n;
    n.text = store(text);
    n.kind = Kind::CData;
    nodes_.push_back(n);
    return static_cast<NodeId>(nodes_.size() - 1);
}

void XmlTree::appendChild(NodeId parent, NodeId child)
{
    Node &p = nodes_[parent];
    if (p.lastChild == kNone)
        p.firstChild = child;
    else
        nodes_[p.lastChild].nextSibling = child;
    p.lastChild = child;
}

XmlTree::AttrId XmlTree::findAttribute(NodeId element, std::string_view name, AttrId *previous) const
{
    AttrId prev = kNone;
    for (AttrId a = nodes_[element].firstAttr; a != kNone; prev = a, a = attrs_[a].next) {
        if (view(attrs_[a].name) == name) {
            if (previous)
                *previous = prev;
            return a;
        }
    }
    return kNone;
}

void XmlTree::appendAttribute(NodeId element, std::string_view name, std::string_view value)
{
    Node &n = nodes_[element];
    Attr attr;
    AttrId prev = kNone;
    if (const AttrId existing = findAttribute(element, name, &prev); existing != kNone) {
        attr.name = attrs_[existing].name;
        const AttrId next = attrs_[existing].next;
        if (prev == kNone)
            n.firstAttr = next;
        else
            attrs_[prev].next = next;
        if (n.lastAttr == existing)
            n.lastAttr = prev;
    } else {
        attr.name = store(name);
    }
    attr.value = store(value);
    attrs_.push_back(attr);
    const AttrId id = static_cast<AttrId>(attrs_.size() - 1);
    if (n.lastAttr == kNone)
        n.firstAttr = id;
    else
        attrs_[n.lastAttr].next = id;
    n.lastAttr = id;
}

void XmlTree::setAttribute(NodeId element, std::string_view name, std::string_view value)
{
    if (const AttrId existing = findAttribute(element, name, nullptr); existing != kNone) {
        const Str stored = store(value);
        attrs_[existing].value = stored;
        return;
    }
    appendAttribute(element, name, value);
}

std::optional<std::string_view> XmlTree::attribute(NodeId node, std::string_view name) const
{
    const AttrId a = findAttribute(node, name, nullptr);
    if (a == kNone)
        return std::nullopt;
    return view(attrs_[a].value);
}

XmlTree::NodeId XmlTree::copyFrom(pugi::xml_node node)
{
    const NodeId element = createElement(node.name());
    for (const pugi::xml_attribute &a : node.attributes())
        appendAttribute(element, a.name(), a.value());
    for (const pugi::xml_node &child : node.children()) {
        switch (child.type()) {
        case pugi::node_element:
            appendChild(element, copyFrom(child));
            break;
        case pugi::node_cdata:
        case pugi::node_pcdata:
            appendChild(element, createCData(child.value()));
            break;
        default:
            break;
        }
    }
    return element;
}

void XmlTree::copyTo(NodeId node, pugi::xml_node parent) const
{
    if (nodes_[node].kind == Kind::CData) {
        parent.append_child(pugi::node_cdata).set_value(std::string(text(node)).c_str());
        return;
    }
    pugi::xml_node el = parent.append_child(std::string(name(node)).c_str());
    for (AttrId a = firstAttribute(node); a != kNone; a = nextAttribute(a))
        el.append_attribute(std::string(attributeName(a)).c_str())
            .set_value(std::string(attributeValue(a)).c_str());
    for (NodeId c = firstChild(node); c != kNone; c = nextSibling(c))
        copyTo(c, el);
}

} // namespace engine::xml
