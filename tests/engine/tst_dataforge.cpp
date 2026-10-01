#include "DcbBuilder.h"

#include "engine/forge/DataForge.h"
#include "engine/forge/DotNetFormat.h"
#include "engine/forge/Exporter.h"
#include "engine/forge/RecordBuilder.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <limits>

using namespace engine;
using namespace testing;

namespace {

std::string g(std::uint8_t seed)
{
    const GuidBytes id = makeGuid(seed);
    return forge::formatGuid(id.data());
}

const std::string kNullError =
    "Error reading array property leaves of type varStrongPointer: System.IndexOutOfRangeException: Index was "
    "outside the bounds of the array.&#xD;&#xA;   at unforge.DataForgeStructDefinition.ReadArrayValueAsXml(XmlNode "
    "parentNode, DataForgePropertyDefinition propertyDefinition, UInt32 firstIndex, UInt16 offset)";

std::vector<std::uint8_t> buildFixture()
{
    DcbBuilder b;
    const auto base = b.addStruct("Base");
    b.addProperty(base, "baseFlag", DataType::Boolean);

    const auto thing = b.addStruct("Thing", base);
    b.addProperty(thing, "count", DataType::Int32);
    b.addProperty(thing, "ratio", DataType::Single);
    b.addProperty(thing, "big", DataType::Double);
    b.addProperty(thing, "label", DataType::String);
    b.addProperty(thing, "loc", DataType::Locale);
    b.addProperty(thing, "kind", DataType::Enum);
    b.addProperty(thing, "id", DataType::Guid);
    b.addProperty(thing, "small", DataType::Int8);
    b.addProperty(thing, "u16", DataType::UInt16);
    b.addProperty(thing, "u64", DataType::UInt64);
    b.addProperty(thing, "emptyText", DataType::String);
    b.addProperty(thing, "raw", DataType::Boolean);

    const auto vec = b.addStruct("Vec");
    b.addProperty(vec, "x", DataType::Single);
    b.addProperty(vec, "y", DataType::Single);

    const auto leaf = b.addStruct("Leaf");
    b.addProperty(leaf, "name", DataType::String);

    const auto holder = b.addStruct("Holder");
    b.addProperty(holder, "pos", DataType::Class, Conversion::Attribute, static_cast<std::uint16_t>(vec));
    b.addProperty(holder, "child", DataType::StrongPointer);
    b.addProperty(holder, "link", DataType::WeakPointer);
    b.addProperty(holder, "nothing", DataType::StrongPointer);
    b.addProperty(holder, "ref", DataType::Reference);
    b.addProperty(holder, "nullRef", DataType::Reference);
    b.addProperty(holder, "tags", DataType::String, Conversion::SimpleArray);
    b.addProperty(holder, "leaves", DataType::StrongPointer, Conversion::ComplexArray);
    b.addProperty(holder, "vecs", DataType::Class, Conversion::ClassArray, static_cast<std::uint16_t>(vec));
    b.addProperty(holder, "flags", DataType::Boolean, Conversion::SimpleArray);
    b.addProperty(holder, "emptyArr", DataType::Int32, Conversion::SimpleArray);

    const auto node = b.addStruct("Node");
    b.addProperty(node, "next", DataType::StrongPointer);

    const auto dup = b.addStruct("Dup", base);
    b.addProperty(dup, "x", DataType::Int32);
    b.addProperty(dup, "baseFlag", DataType::UInt8); // same name as the inherited Bool

    b.addInstance(thing, Inst()
                             .u8(1)
                             .i32(-5)
                             .f32(1.5f)
                             .f64(0.1)
                             .u32(b.text("hello & <bye>"))
                             .u32(b.text("@loc_key"))
                             .u32(b.text("Enum_Value"))
                             .guid(makeGuid(0x40))
                             .i8(-3)
                             .u16(65535)
                             .u64(std::numeric_limits<std::uint64_t>::max())
                             .u32(0)
                             .u8(2));
    b.addInstance(vec, Inst().f32(1).f32(2));
    b.addInstance(vec, Inst().f32(3).f32(4.25f));
    b.addInstance(leaf, Inst().u32(b.text("first")));
    b.addInstance(leaf, Inst().u32(b.text("second")));

    const auto tags = b.addPool(Pool::String, {Inst().u32(b.text("a")), Inst().u32(b.text("b"))});
    const auto leaves = b.addPool(Pool::StrongPointer, {Inst().ptr(leaf, 1), Inst().nullPtr()});
    const auto flags = b.addPool(Pool::Boolean, {Inst().u8(1), Inst().u8(0)});
    b.addInstance(holder, Inst()
                              .f32(0.5f)
                              .f32(-0.5f)
                              .ptr(leaf, 0)
                              .ptr(leaf, 1)
                              .nullPtr()
                              .ref(makeGuid(0x10))
                              .ref(GuidBytes{})
                              .array(2, tags)
                              .array(2, leaves)
                              .array(2, 0)
                              .array(2, flags)
                              .array(0, 0));
    b.addInstance(node, Inst().ptr(node, 1));
    b.addInstance(node, Inst().ptr(node, 0));
    b.addInstance(dup, Inst().u8(1).i32(7).u8(9));

    b.addRecord("Thing.Alpha", "libs/foundry/records/things/alpha.xml", thing, 0, makeGuid(0x10));
    b.addRecord("Holder.H1", "libs/foundry/records/holder.xml", holder, 0, makeGuid(0x20));
    b.addRecord("Node.Loop", "libs/foundry/records/loop.xml", node, 0, makeGuid(0x30));
    b.addRecord("Bad Name.X", "libs/foundry/records/bad.xml", leaf, 0, makeGuid(0x50));
    b.addRecord("Leaf.One", "libs/foundry/records/leaf.xml", leaf, 0, makeGuid(0x60));
    b.addRecord("Leaf.Two", "libs/foundry/records/leaf.xml", leaf, 1, makeGuid(0x70));
    b.addRecord("Dup.D", "libs/foundry/records/dup.xml", dup, 0, makeGuid(0x80));
    return b.build();
}

forge::DataForge loadFixture()
{
    auto df = forge::DataForge::load(buildFixture());
    if (!df)
        qFatal("fixture failed to load: %s", df.error().message.c_str());
    return std::move(*df);
}

std::optional<std::string> recordXml(const forge::DataForge &df, std::string_view path, forge::BuildOptions options = {})
{
    forge::RecordBuilder builder(df, options);
    xml::XmlTree tree;
    std::string out;
    const auto index = df.recordByPath(path);
    if (!index || !forge::writeRecordXml(builder, tree, *index, out))
        return std::nullopt;
    return out;
}

std::string holderXml(bool nullErrors)
{
    std::string leaves = "  <leaves>\r\n"
                         "    <Leaf name=\"second\" />\r\n";
    if (nullErrors)
        leaves += "    <leaves value=\"" + kNullError + "\" />\r\n";
    leaves += "  </leaves>\r\n";
    return "<Holder.H1 ref=\"" + g(0x10) + "\" __type=\"Holder\" __ref=\"" + g(0x20) +
           "\" __path=\"libs/foundry/records/holder.xml\" __team=\"Unknown\">\r\n"
           "  <pos x=\"0.5\" y=\"-0.5\" />\r\n"
           "  <child>\r\n"
           "    <Leaf name=\"first\" />\r\n"
           "  </child>\r\n"
           "  <link value=\"Leaf[0001]\" />\r\n"
           "  <tags>\r\n"
           "    <String value=\"a\" />\r\n"
           "    <String value=\"b\" />\r\n"
           "  </tags>\r\n" +
           leaves +
           "  <vecs>\r\n"
           "    <Vec x=\"1\" y=\"2\" />\r\n"
           "    <Vec x=\"3\" y=\"4.25\" />\r\n"
           "  </vecs>\r\n"
           "  <flags>\r\n"
           "    <Bool value=\"1\" />\r\n"
           "    <Bool value=\"0\" />\r\n"
           "  </flags>\r\n"
           "</Holder.H1>";
}

} // namespace

class TestDataForge : public QObject
{
    Q_OBJECT

private slots:
    void loadsTables()
    {
        const auto df = loadFixture();
        QCOMPARE(df.version(), 8);
        QCOMPARE(df.structs().size(), std::size_t(7));
        QCOMPARE(df.records().size(), std::size_t(7));
        QCOMPARE(df.structName(1), std::string("Thing"));
        // Inherited properties come first.
        QCOMPARE(df.propertyName(df.structProperties(1).front()), std::string("baseFlag"));
        QCOMPARE(df.structProperties(1).size(), std::size_t(13));
    }

    void lastRecordWinsPerPath()
    {
        const auto df = loadFixture();
        QCOMPARE(df.recordByPath("libs/foundry/records/leaf.xml").value_or(99), std::uint32_t(5));
        QCOMPARE(df.fileRecords(), (std::vector<std::uint32_t>{0, 1, 2, 3, 5, 6}));
        QCOMPARE(df.recordByGuid(makeGuid(0x60)).value_or(99), std::uint32_t(4));
    }

    void scalarAttributes()
    {
        const auto df = loadFixture();
        const std::string expected =
            "<Thing.Alpha baseFlag=\"1\" count=\"-5\" ratio=\"1.5\" big=\"0.1\" label=\"hello &amp; &lt;bye&gt;\" "
            "loc=\"@loc_key\" kind=\"Enum_Value\" id=\"" + g(0x40) + "\" small=\"-3\" u16=\"65535\" "
            "u64=\"18446744073709551615\" raw=\"2\" __type=\"Thing\" __ref=\"" + g(0x10) +
            "\" __path=\"libs/foundry/records/things/alpha.xml\" __team=\"Unknown\" />";
        QCOMPARE(recordXml(df, "libs/foundry/records/things/alpha.xml").value_or("<skipped>"), expected);
    }

    void structuresPointersAndArrays()
    {
        const auto df = loadFixture();
        QCOMPARE(recordXml(df, "libs/foundry/records/holder.xml").value_or("<skipped>"), holderXml(true));
    }

    void cleanModeSkipsNullArrayEntries()
    {
        const auto df = loadFixture();
        forge::BuildOptions clean;
        clean.nullArrayErrors = false;
        QCOMPARE(recordXml(df, "libs/foundry/records/holder.xml", clean).value_or("<skipped>"), holderXml(false));
    }

    // A pointer cycle prunes every element on the loop, so the record renders
    // empty and unforge writes no file for it.
    void cyclesRenderEmpty()
    {
        const auto df = loadFixture();
        QVERIFY(!recordXml(df, "libs/foundry/records/loop.xml"));
    }

    void invalidRecordNameBecomesError()
    {
        const auto df = loadFixture();
        QCOMPARE(recordXml(df, "libs/foundry/records/bad.xml").value_or("<skipped>"),
                 std::string("<Error value=\"The ' ' character, hexadecimal value 0x20, cannot be included in a "
                             "name.\" />"));
    }

    // A derived property with an inherited name replaces the earlier
    // attribute and moves to the end (XmlAttributeCollection.Append).
    void duplicateAttributeNames()
    {
        const auto df = loadFixture();
        QCOMPARE(recordXml(df, "libs/foundry/records/dup.xml").value_or("<skipped>"),
                 "<Dup.D x=\"7\" baseFlag=\"9\" __type=\"Dup\" __ref=\"" + g(0x80) +
                     "\" __path=\"libs/foundry/records/dup.xml\" __team=\"Unknown\" />");
    }

    void exportsFiles()
    {
        const auto df = loadFixture();
        QTemporaryDir out;
        forge::ExportOptions options;
        options.threads = 2;
        auto stats = forge::exportRecords(df, std::filesystem::path(out.path().toStdWString()), options);
        QVERIFY(stats);
        QCOMPARE(stats->written, std::size_t(5));
        QCOMPARE(stats->skipped, std::size_t(1)); // the cycle
        QVERIFY(stats->failures.empty());

        QFile f(out.filePath("libs/foundry/records/holder.xml"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll().toStdString(), holderXml(true));
        QVERIFY(!QFile::exists(out.filePath("libs/foundry/records/loop.xml")));

        // Filtered export.
        QTemporaryDir only;
        options.include = [](std::string_view path) { return path.starts_with("libs/foundry/records/things/"); };
        stats = forge::exportRecords(df, std::filesystem::path(only.path().toStdWString()), options);
        QVERIFY(stats);
        QCOMPARE(stats->written, std::size_t(1));
        QVERIFY(QFile::exists(only.filePath("libs/foundry/records/things/alpha.xml")));
    }

    void rejectsBadFiles()
    {
        QVERIFY(!forge::DataForge::load({1, 2, 3}));
        auto bytes = buildFixture();
        bytes.resize(bytes.size() / 2);
        QVERIFY(!forge::DataForge::load(std::move(bytes)));
    }
};

QTEST_GUILESS_MAIN(TestDataForge)
#include "tst_dataforge.moc"
