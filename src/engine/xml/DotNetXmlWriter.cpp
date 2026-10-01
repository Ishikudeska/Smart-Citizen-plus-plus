#include "engine/xml/DotNetXmlWriter.h"

#include <cstdio>

namespace engine::xml {

namespace {

constexpr std::string_view kNewLine = "\r\n";

void indent(std::string &out, int depth)
{
    out += kNewLine;
    out.append(static_cast<std::size_t>(depth) * 2, ' ');
}

void appendCharRef(std::string &out, unsigned char c)
{
    char buf[8];
    const int n = std::snprintf(buf, sizeof buf, "&#x%X;", c);
    out.append(buf, static_cast<std::size_t>(n));
}

void appendAttributeValue(std::string &out, std::string_view value)
{
    for (const char ch : value) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        case '\t': out += "&#x9;"; break;
        case '\n': out += "&#xA;"; break;
        case '\r': out += "&#xD;"; break;
        default:
            if (c < 0x20)
                appendCharRef(out, c);
            else
                out.push_back(ch);
        }
    }
}

// NewLineHandling.Replace turns \r\n, \r and \n into CRLF inside CDATA, and
// "]]>" has to be split across two sections.
void appendCData(std::string &out, std::string_view text)
{
    out += "<![CDATA[";
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\r') {
            out += kNewLine;
            if (i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
        } else if (c == '\n') {
            out += kNewLine;
        } else if (c == ']' && text.substr(i, 3) == "]]>") {
            out += "]]]]><![CDATA[>";
            i += 2;
        } else {
            out.push_back(c);
        }
    }
    out += "]]>";
}

void writeElement(const XmlTree &tree, XmlTree::NodeId node, int depth, bool parentMixed, std::string &out)
{
    if (depth > 0 && !parentMixed)
        indent(out, depth);
    out.push_back('<');
    out += tree.name(node);
    for (auto a = tree.firstAttribute(node); a != XmlTree::kNone; a = tree.nextAttribute(a)) {
        out.push_back(' ');
        out += tree.attributeName(a);
        out += "=\"";
        appendAttributeValue(out, tree.attributeValue(a));
        out.push_back('"');
    }
    if (tree.firstChild(node) == XmlTree::kNone) {
        out += " />";
        return;
    }
    out.push_back('>');

    // The root never inherits mixed content; deeper elements inherit their
    // parent's state at the time they start.
    bool mixed = depth == 0 ? false : parentMixed;
    for (auto c = tree.firstChild(node); c != XmlTree::kNone; c = tree.nextSibling(c)) {
        if (tree.kind(c) == XmlTree::Kind::CData) {
            appendCData(out, tree.text(c));
            mixed = true;
        } else {
            writeElement(tree, c, depth + 1, mixed, out);
        }
    }
    if (!mixed)
        indent(out, depth);
    out += "</";
    out += tree.name(node);
    out.push_back('>');
}

bool isNameChar(unsigned char c)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
        return true;
    if (c == '.' || c == '-' || c == '_' || c == 0xB7)
        return true;
    return (c >= 0xC0 && c <= 0xD6) || (c >= 0xD8 && c <= 0xF6) || c >= 0xF8;
}

// XmlDocument.CheckName: every character must be an NCName character. It
// does not check the first character specially.
std::optional<std::string> checkPart(std::string_view part)
{
    for (const char ch : part) {
        const auto c = static_cast<unsigned char>(ch);
        if (isNameChar(c))
            continue;
        std::string shown;
        if (c == 0)
            shown = ".";
        else
            appendLatin1AsUtf8(shown, std::string_view(&ch, 1));
        char hex[8];
        std::snprintf(hex, sizeof hex, "0x%02X", c);
        return "The '" + shown + "' character, hexadecimal value " + hex + ", cannot be included in a name.";
    }
    return std::nullopt;
}

} // namespace

void writeDotNet(const XmlTree &tree, XmlTree::NodeId root, std::string &out)
{
    writeElement(tree, root, 0, false, out);
}

void appendLatin1AsUtf8(std::string &out, std::string_view latin1)
{
    for (const char ch : latin1) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 0x80) {
            out.push_back(ch);
        } else {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
}

std::optional<std::string> dotNetNameError(std::string_view latin1)
{
    // XmlNode.SplitName: a colon that is neither first nor last separates a
    // prefix; otherwise the whole name is the local name.
    std::string_view prefix;
    std::string_view local = latin1;
    if (const auto colon = latin1.find(':');
        colon != std::string_view::npos && colon != 0 && colon != latin1.size() - 1) {
        prefix = latin1.substr(0, colon);
        local = latin1.substr(colon + 1);
    }
    if (auto error = checkPart(prefix))
        return error;
    if (auto error = checkPart(local))
        return error;
    if (local.empty())
        return std::string("The local name for elements or attributes cannot be null or an empty string.");
    return std::nullopt;
}

} // namespace engine::xml
