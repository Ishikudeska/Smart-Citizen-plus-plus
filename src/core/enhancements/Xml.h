#pragma once

#include <QString>

#include <pugixml.hpp>

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

// lxml.etree, as the enhancements generator uses it, over pugixml: parse a
// record, ElementPath find/findall ("tag", "a/b", ".//tag",
// ".//tag[@attr='v']"), iter() in document order including the element
// itself, get() that tells a missing attribute from an empty one.
namespace core::enh {

using Node = pugi::xml_node;

// A parsed DataForge record; null when the file is missing or not XML
// (lxml's ParseError).
class XmlDoc
{
public:
    XmlDoc() = default;
    static XmlDoc load(const QString &path);
    static XmlDoc parse(std::string_view text); // tests

    explicit operator bool() const { return doc_ != nullptr; }
    Node root() const { return doc_ ? doc_->document_element() : Node(); }

private:
    std::shared_ptr<pugi::xml_document> doc_;
};

inline std::string_view tag(Node n)
{
    return n.name();
}

// el.get(name): nothing when the attribute is absent.
std::optional<std::string_view> get(Node n, const char *name);
// el.get(name, default).
std::string_view getOr(Node n, const char *name, std::string_view fallback = {});

QString qs(std::string_view utf8);
inline QString qs(const std::optional<std::string_view> &v)
{
    return v ? qs(*v) : QString();
}

// ElementPath.
Node find(Node n, std::string_view path);
std::vector<Node> findAll(Node n, std::string_view path);

// el.iter(tag) / el.iter(): the element and its descendants, document order.
std::vector<Node> iter(Node n, std::string_view tag = {});
template <class Visit> void forEachElement(Node n, Visit &&visit) // visit(Node) -> bool: false stops
{
    if (!n)
        return;
    Node cur = n;
    while (true) {
        if (cur.type() == pugi::node_element && !visit(cur))
            return;
        if (Node child = cur.first_child()) {
            cur = child;
            continue;
        }
        while (cur != n && !cur.next_sibling())
            cur = cur.parent();
        if (cur == n)
            return;
        cur = cur.next_sibling();
    }
}

// list(el): element children.
std::vector<Node> children(Node n);

// The generator's helpers.
Node findDescendant(Node root, std::string_view tagName); // _find: ".//tag"
Node findByType(Node root, std::string_view typeName);    // _find_by_type
std::optional<std::string_view> attrOf(Node root, std::string_view tagName, const char *attr); // _attr

} // namespace core::enh
