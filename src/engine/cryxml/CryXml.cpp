#include "engine/cryxml/CryXml.h"

#include "engine/xml/DotNetXmlWriter.h"

#include <cstring>
#include <unordered_map>
#include <vector>

namespace engine::cryxml {

namespace {

constexpr std::size_t kNodeSize = 28;
constexpr std::size_t kAttributeSize = 8;

struct Node
{
    std::int32_t nameOffset;
    std::int32_t contentOffset;
    std::int16_t attributeCount;
    std::int32_t parent;
};

class Reader
{
public:
    Reader(std::span<const std::uint8_t> data, bool swap)
        : data_(data)
        , swap_(swap)
    {
    }

    bool i32(std::size_t pos, std::int32_t &out) const
    {
        if (pos + 4 > data_.size())
            return false;
        std::uint32_t v;
        std::memcpy(&v, data_.data() + pos, 4);
        if (swap_)
            v = (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
        out = static_cast<std::int32_t>(v);
        return true;
    }

    bool i16(std::size_t pos, std::int16_t &out) const
    {
        if (pos + 2 > data_.size())
            return false;
        std::uint16_t v;
        std::memcpy(&v, data_.data() + pos, 2);
        if (swap_)
            v = static_cast<std::uint16_t>((v >> 8) | (v << 8));
        out = static_cast<std::int16_t>(v);
        return true;
    }

private:
    std::span<const std::uint8_t> data_;
    bool swap_;
};

// .NET's char.IsWhiteSpace over Latin-1.
bool isBlank(std::string_view latin1)
{
    for (const char ch : latin1) {
        const auto c = static_cast<unsigned char>(ch);
        if (!(c == ' ' || (c >= 0x09 && c <= 0x0D) || c == 0x85 || c == 0xA0))
            return false;
    }
    return true;
}

Error formatError(std::string message)
{
    return Error{Errc::Format, "CryXml: " + std::move(message)};
}

} // namespace

bool isCryXml(std::span<const std::uint8_t> data)
{
    auto startsWith = [&](const char *magic) {
        const std::size_t n = std::strlen(magic);
        return data.size() >= n && std::memcmp(data.data(), magic, n) == 0;
    };
    return startsWith("CryXml") || startsWith("CRY3SDK");
}

bool isPlainXml(std::span<const std::uint8_t> data)
{
    std::size_t i = 0;
    if (data.size() >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
        i = 3;
    while (i < data.size() && (data[i] == ' ' || data[i] == '\t' || data[i] == '\r' || data[i] == '\n'))
        ++i;
    return i < data.size() && data[i] == '<';
}

Result<xml::XmlTree::NodeId> parse(std::span<const std::uint8_t> data, xml::XmlTree &tree)
{
    tree.clear();
    if (data.empty() || data[0] != 'C')
        return std::unexpected(formatError("Unknown File Format"));

    // ReadFString(7): up to 7 bytes, cut at the first NUL.
    const std::size_t fixedLen = std::min<std::size_t>(7, data.size());
    std::size_t magicLen = 0;
    while (magicLen < fixedLen && data[magicLen] != 0)
        ++magicLen;
    const std::string_view magic(reinterpret_cast<const char *>(data.data()), magicLen);

    std::size_t headerLength = fixedLen;
    if (magic == "CryXml" || magic == "CryXmlB") {
        // ReadCString: through the next NUL. For the 6-character "CryXml"
        // this over-reads into the header, as unforge does.
        while (headerLength < data.size() && data[headerLength] != 0)
            ++headerLength;
        if (headerLength < data.size())
            ++headerLength;
    } else if (magic == "CRY3SDK") {
        headerLength += 2;
    } else {
        return std::unexpected(formatError("Unknown File Format"));
    }

    // unforge's "BigEndian" read is really a plain little-endian read; it
    // falls back to swapped order when the length field doesn't match.
    std::int32_t fileLength = 0;
    bool swap = false;
    if (!Reader(data, false).i32(headerLength, fileLength))
        return std::unexpected(formatError("header is truncated"));
    if (static_cast<std::uint64_t>(fileLength) != data.size())
        swap = true;
    const Reader r(data, swap);

    std::int32_t h[9];
    for (int i = 0; i < 9; ++i)
        if (!r.i32(headerLength + i * 4, h[i]))
            return std::unexpected(formatError("header is truncated"));
    const std::int32_t nodeTableOffset = h[1], nodeCount = h[2];
    const std::int32_t attributeTableOffset = h[3], attributeCount = h[4];
    const std::int32_t stringTableOffset = h[7];
    auto fits = [&](std::int32_t offset, std::int32_t count, std::size_t size) {
        return offset >= 0 && count >= 0 &&
               static_cast<std::uint64_t>(offset) + static_cast<std::uint64_t>(count) * size <= data.size();
    };
    if (!fits(nodeTableOffset, nodeCount, kNodeSize) || !fits(attributeTableOffset, attributeCount, kAttributeSize) ||
        !fits(stringTableOffset, 0, 1))
        return std::unexpected(formatError("header offsets are out of range"));

    std::vector<Node> nodes(static_cast<std::size_t>(nodeCount));
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const std::size_t at = static_cast<std::size_t>(nodeTableOffset) + i * kNodeSize;
        Node &n = nodes[i];
        std::int16_t childCount = 0;
        if (!r.i32(at, n.nameOffset) || !r.i32(at + 4, n.contentOffset) || !r.i16(at + 8, n.attributeCount) ||
            !r.i16(at + 10, childCount) || !r.i32(at + 12, n.parent))
            return std::unexpected(formatError("node table is truncated"));
    }
    std::vector<std::pair<std::int32_t, std::int32_t>> attributes(static_cast<std::size_t>(attributeCount));
    for (std::size_t i = 0; i < attributes.size(); ++i) {
        const std::size_t at = static_cast<std::size_t>(attributeTableOffset) + i * kAttributeSize;
        if (!r.i32(at, attributes[i].first) || !r.i32(at + 4, attributes[i].second))
            return std::unexpected(formatError("attribute table is truncated"));
    }

    // The string table runs from its offset to the end of the file (the
    // size field is ignored). Empty strings map to null.
    struct Str
    {
        std::string_view text;
        bool null;
    };
    std::unordered_map<std::int32_t, Str> strings;
    const char *base = reinterpret_cast<const char *>(data.data()) + stringTableOffset;
    const std::size_t tableSize = data.size() - static_cast<std::size_t>(stringTableOffset);
    for (std::size_t pos = 0; pos < tableSize;) {
        std::size_t end = pos;
        while (end < tableSize && base[end] != '\0')
            ++end;
        strings[static_cast<std::int32_t>(pos)] = {std::string_view(base + pos, end - pos), end == pos};
        pos = end + 1;
    }

    std::string utf8;
    auto toUtf8 = [&utf8](std::string_view latin1) -> const std::string & {
        utf8.clear();
        xml::appendLatin1AsUtf8(utf8, latin1);
        return utf8;
    };

    std::unordered_map<std::int32_t, xml::XmlTree::NodeId> created;
    xml::XmlTree::NodeId root = xml::XmlTree::kNone;
    std::size_t attributeIndex = 0;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const Node &n = nodes[i];
        const auto name = strings.find(n.nameOffset);
        if (name == strings.end())
            return std::unexpected(formatError("node name offset " + std::to_string(n.nameOffset) + " is not a string"));
        if (auto error = xml::dotNetNameError(name->second.text))
            return std::unexpected(formatError(*error));
        const auto element = tree.createElement(toUtf8(name->second.text));

        for (int a = 0; a < n.attributeCount; ++a) {
            if (attributeIndex >= attributes.size())
                return std::unexpected(formatError("attribute index is out of range"));
            const auto [nameOffset, valueOffset] = attributes[attributeIndex++];
            const auto attrName = strings.find(nameOffset);
            if (attrName == strings.end())
                return std::unexpected(formatError("attribute name offset is not a string"));
            if (auto error = xml::dotNetNameError(attrName->second.text))
                return std::unexpected(formatError(*error));
            const std::string nameUtf8 = toUtf8(attrName->second.text);
            const auto value = strings.find(valueOffset);
            tree.setAttribute(element, nameUtf8, value == strings.end() ? "BUGGED" : toUtf8(value->second.text));
        }

        if (const auto content = strings.find(n.contentOffset); content != strings.end()) {
            if (!content->second.null && !isBlank(content->second.text))
                tree.appendChild(element, tree.createCData(toUtf8(content->second.text)));
        } else {
            tree.appendChild(element, tree.createCData("BUGGED"));
        }

        created[static_cast<std::int32_t>(i)] = element;
        if (const auto parent = created.find(n.parent); parent != created.end() && n.parent != static_cast<std::int32_t>(i)) {
            tree.appendChild(parent->second, element);
        } else if (root == xml::XmlTree::kNone) {
            root = element;
        } else {
            return std::unexpected(formatError("This document already has a 'DocumentElement' node."));
        }
    }
    if (root == xml::XmlTree::kNone)
        return std::unexpected(formatError("no root element"));
    return root;
}

Result<std::string> toXml(std::span<const std::uint8_t> data)
{
    if (!isCryXml(data))
        return std::string(reinterpret_cast<const char *>(data.data()), data.size());
    xml::XmlTree tree;
    auto root = parse(data, tree);
    if (!root)
        return std::unexpected(root.error());
    std::string out;
    xml::writeDotNet(tree, *root, out);
    return out;
}

} // namespace engine::cryxml
