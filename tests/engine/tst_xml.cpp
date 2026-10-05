#include "engine/xml/DotNetXmlWriter.h"
#include "engine/xml/XmlTree.h"

#include <QTest>

#include <pugixml.hpp>

using namespace engine::xml;

namespace {

std::string write(const XmlTree &tree, XmlTree::NodeId root)
{
    std::string out;
    writeDotNet(tree, root, out);
    return out;
}

} // namespace

class TestXml : public QObject
{
    Q_OBJECT

private slots:
    void indentationAndEmptyElements()
    {
        XmlTree t;
        const auto root = t.createElement("Root");
        t.appendAttribute(root, "a", "1");
        const auto child = t.createElement("Child");
        t.appendAttribute(child, "b", "2");
        const auto grand = t.createElement("Grand");
        t.appendAttribute(grand, "c", "3");
        t.appendChild(child, grand);
        t.appendChild(root, child);
        const auto empty = t.createElement("Empty");
        t.appendAttribute(empty, "d", "4");
        t.appendChild(root, empty);

        QCOMPARE(write(t, root), std::string("<Root a=\"1\">\r\n"
                                             "  <Child b=\"2\">\r\n"
                                             "    <Grand c=\"3\" />\r\n"
                                             "  </Child>\r\n"
                                             "  <Empty d=\"4\" />\r\n"
                                             "</Root>"));
    }

    void attributeEscaping()
    {
        XmlTree t;
        const auto root = t.createElement("R");
        t.appendAttribute(root, "v", "a&b<c>d\"e'f\tg\r\nh\x01i");
        QCOMPARE(write(t, root),
                 std::string("<R v=\"a&amp;b&lt;c&gt;d&quot;e'f&#x9;g&#xD;&#xA;h&#x1;i\" />"));
    }

    void nonAsciiStaysUtf8()
    {
        XmlTree t;
        const auto root = t.createElement("R");
        std::string value;
        appendLatin1AsUtf8(value, "20\xB0");
        t.appendAttribute(root, "v", value);
        QCOMPARE(write(t, root), std::string("<R v=\"20\xC2\xB0\" />"));
    }

    // XmlAttributeCollection.Append moves a re-added name to the end;
    // SetAttribute replaces in place.
    void attributeAppendSemantics()
    {
        XmlTree t;
        const auto root = t.createElement("R");
        t.appendAttribute(root, "a", "1");
        t.appendAttribute(root, "b", "2");
        t.appendAttribute(root, "a", "3");
        QCOMPARE(write(t, root), std::string("<R b=\"2\" a=\"3\" />"));

        t.setAttribute(root, "b", "4");
        QCOMPARE(write(t, root), std::string("<R b=\"4\" a=\"3\" />"));
        QCOMPARE(t.attribute(root, "b").value_or(""), std::string_view("4"));
    }

    // After CDATA the rest of the element is written without indentation.
    void mixedContent()
    {
        XmlTree t;
        const auto root = t.createElement("R");
        const auto a = t.createElement("A");
        t.appendChild(a, t.createCData("text\nmore ]]> end"));
        const auto b = t.createElement("B");
        t.appendAttribute(b, "x", "1");
        t.appendChild(a, b);
        t.appendChild(root, a);
        QCOMPARE(write(t, root),
                 std::string("<R>\r\n"
                             "  <A><![CDATA[text\r\nmore ]]]]><![CDATA[> end]]><B x=\"1\" /></A>\r\n"
                             "</R>"));
    }

    void copiesToPugi()
    {
        XmlTree t;
        const auto root = t.createElement("R");
        t.appendAttribute(root, "a", "1");
        const auto c = t.createElement("C");
        t.appendChild(c, t.createCData("hi"));
        t.appendChild(root, c);
        pugi::xml_document doc;
        t.copyTo(root, doc);
        QCOMPARE(std::string(doc.child("R").attribute("a").value()), std::string("1"));
        QCOMPARE(std::string(doc.child("R").child("C").text().get()), std::string("hi"));
    }

    void dotNetNameErrors_data()
    {
        QTest::addColumn<QByteArray>("name");
        QTest::addColumn<QString>("error");
        QTest::newRow("plain") << QByteArray("EntityClassDefinition.AEGS_Gladius") << QString();
        QTest::newRow("digit first is fine") << QByteArray("1abc") << QString();
        QTest::newRow("prefix") << QByteArray("ss:ID") << QString();
        QTest::newRow("space") << QByteArray("bar stools")
                               << "The ' ' character, hexadecimal value 0x20, cannot be included in a name.";
        QTest::newRow("ampersand")
            << QByteArray("Tram&Myers")
            << "The '&' character, hexadecimal value 0x26, cannot be included in a name.";
        QTest::newRow("leading colon")
            << QByteArray(":a") << "The ':' character, hexadecimal value 0x3A, cannot be included in a name.";
        QTest::newRow("latin1 letter") << QByteArray("caf\xE9") << QString();
        QTest::newRow("multiply sign")
            << QByteArray("a\xD7"
                          "b")
            << QString::fromUtf8("The '\xC3\x97' character, hexadecimal value 0xD7, cannot "
                                 "be included in a name.");
        QTest::newRow("empty")
            << QByteArray("")
            << "The local name for elements or attributes cannot be null or an empty string.";
    }

    void dotNetNameErrors()
    {
        QFETCH(QByteArray, name);
        QFETCH(QString, error);
        const auto result = dotNetNameError(std::string_view(name.constData(), name.size()));
        QCOMPARE(result.has_value(), !error.isNull());
        if (result)
            QCOMPARE(QString::fromStdString(*result), error);
    }
};

QTEST_APPLESS_MAIN(TestXml)
#include "tst_xml.moc"
