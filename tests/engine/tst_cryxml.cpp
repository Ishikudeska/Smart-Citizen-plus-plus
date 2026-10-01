#include "engine/cryxml/CryXml.h"

#include <QTest>

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace engine;

namespace {

// Builds a CryXmlB file. Nodes are (name, content, parent, attributes).
struct Node
{
    std::string name;
    std::string content;
    int parent;
    std::vector<std::pair<std::string, std::string>> attributes;
};

std::vector<std::uint8_t> buildCryXml(const std::vector<Node> &nodes, bool bigEndian, const std::string &magic = "CryXmlB")
{
    std::string strings(1, '\0'); // offset 0 = empty string
    std::map<std::string, std::int32_t> offsets{{"", 0}};
    auto intern = [&](const std::string &s) {
        if (auto it = offsets.find(s); it != offsets.end())
            return it->second;
        const auto off = static_cast<std::int32_t>(strings.size());
        strings += s;
        strings.push_back('\0');
        offsets[s] = off;
        return off;
    };

    std::vector<std::uint8_t> out(magic.begin(), magic.end());
    if (magic == "CRY3SDK")
        out.insert(out.end(), {0, 0});
    else
        out.push_back(0);
    const std::size_t headerAt = out.size();
    out.resize(headerAt + 36);

    auto put32 = [&](std::vector<std::uint8_t> &o, std::int32_t v) {
        std::uint32_t u = static_cast<std::uint32_t>(v);
        if (bigEndian)
            u = (u >> 24) | ((u >> 8) & 0xFF00) | ((u << 8) & 0xFF0000) | (u << 24);
        const auto *p = reinterpret_cast<const std::uint8_t *>(&u);
        o.insert(o.end(), p, p + 4);
    };
    auto put16 = [&](std::vector<std::uint8_t> &o, std::int16_t v) {
        std::uint16_t u = static_cast<std::uint16_t>(v);
        if (bigEndian)
            u = static_cast<std::uint16_t>((u >> 8) | (u << 8));
        const auto *p = reinterpret_cast<const std::uint8_t *>(&u);
        o.insert(o.end(), p, p + 2);
    };

    const auto nodeTable = static_cast<std::int32_t>(out.size());
    int attrIndex = 0;
    for (const auto &n : nodes) {
        put32(out, intern(n.name));
        put32(out, intern(n.content));
        put16(out, static_cast<std::int16_t>(n.attributes.size()));
        put16(out, 0);
        put32(out, n.parent);
        put32(out, attrIndex);
        put32(out, 0);
        put32(out, 0);
        attrIndex += static_cast<int>(n.attributes.size());
    }
    const auto attrTable = static_cast<std::int32_t>(out.size());
    for (const auto &n : nodes)
        for (const auto &[k, v] : n.attributes) {
            put32(out, intern(k));
            put32(out, intern(v));
        }
    const auto childTable = static_cast<std::int32_t>(out.size());
    const auto stringTable = static_cast<std::int32_t>(out.size());
    out.insert(out.end(), strings.begin(), strings.end());

    std::vector<std::uint8_t> header;
    put32(header, static_cast<std::int32_t>(out.size()));
    put32(header, nodeTable);
    put32(header, static_cast<std::int32_t>(nodes.size()));
    put32(header, attrTable);
    put32(header, attrIndex);
    put32(header, childTable);
    put32(header, 0);
    put32(header, stringTable);
    put32(header, static_cast<std::int32_t>(strings.size()));
    std::memcpy(out.data() + headerAt, header.data(), header.size());
    return out;
}

const std::vector<Node> kShip = {
    {"Root", "", -1, {{"version", "2"}, {"name", "ship"}}},
    {"Part", "some text", 0, {{"name", "nose"}}},
    {"Part", "  \r\n ", 0, {{"name", "tail"}}}, // blank content is dropped
};

const std::string kShipXml = "<Root version=\"2\" name=\"ship\">\r\n"
                             "  <Part name=\"nose\"><![CDATA[some text]]></Part>\r\n"
                             "  <Part name=\"tail\" />\r\n"
                             "</Root>";

std::string asString(const Result<std::string> &r)
{
    return r ? *r : "error: " + r.error().message;
}

} // namespace

class TestCryXml : public QObject
{
    Q_OBJECT

private slots:
    void littleEndian()
    {
        const auto bytes = buildCryXml(kShip, false);
        QVERIFY(cryxml::isCryXml(bytes));
        QCOMPARE(asString(cryxml::toXml(bytes)), kShipXml);
    }

    void bigEndianDetectedFromLength()
    {
        QCOMPARE(asString(cryxml::toXml(buildCryXml(kShip, true))), kShipXml);
    }

    void cry3sdkHeader()
    {
        QCOMPARE(asString(cryxml::toXml(buildCryXml(kShip, false, "CRY3SDK"))), kShipXml);
    }

    void plainXmlPassesThrough()
    {
        const std::string text = "\xEF\xBB\xBF<a b=\"1\"/>";
        const std::vector<std::uint8_t> bytes(text.begin(), text.end());
        QVERIFY(!cryxml::isCryXml(bytes));
        QVERIFY(cryxml::isPlainXml(bytes));
        QCOMPARE(asString(cryxml::toXml(bytes)), text);
    }

    void rejectsGarbage()
    {
        const std::vector<std::uint8_t> bytes = {'C', 'r', 'a', 'p', 0, 1, 2, 3, 4, 5, 6, 7};
        xml::XmlTree tree;
        auto root = cryxml::parse(bytes, tree);
        QVERIFY(!root);
        QCOMPARE(root.error().code, Errc::Format);
    }

    void truncatedTablesFail()
    {
        auto bytes = buildCryXml(kShip, false);
        bytes.resize(60);
        xml::XmlTree tree;
        QVERIFY(!cryxml::parse(bytes, tree));
    }
};

QTEST_APPLESS_MAIN(TestCryXml)
#include "tst_cryxml.moc"
