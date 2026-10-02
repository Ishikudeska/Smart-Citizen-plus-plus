// TagBuilder against values produced by the original tag_builder.py
// (tests/fixtures/tag_builder.json, from tools/parity/gen_tag_fixtures.py;
// the cases here mirror that script's), plus the settings-load migrations.

#include "core/tags/TagBuilder.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace core::tags;

namespace {

QJsonObject fixture()
{
    QFile f(QStringLiteral(SC_SOURCE_DIR "/tests/fixtures/tag_builder.json"));
    if (!f.open(QIODevice::ReadOnly))
        qFatal("missing tag_builder.json fixture");
    return QJsonDocument::fromJson(f.readAll()).object();
}

QHash<QString, QString> values(std::initializer_list<std::pair<const char *, const char *>> pairs)
{
    QHash<QString, QString> v;
    for (const auto &[k, x] : pairs)
        v.insert(QString::fromUtf8(k), QString::fromUtf8(x));
    return v;
}

QSet<QString> keys(std::initializer_list<const char *> list)
{
    QSet<QString> s;
    for (const char *k : list)
        s.insert(QString::fromLatin1(k));
    return s;
}

} // namespace

class TestTags : public QObject
{
    Q_OBJECT

    QJsonObject expected_;

private slots:
    void initTestCase() { expected_ = fixture(); }

    void defaultConfigsSerializeLikePython()
    {
        const QJsonObject json = expected_.value(QStringLiteral("json")).toObject();
        for (const QString &category : kCategories)
            QCOMPARE(defaultConfig(category).toJson(), json.value(category).toString());
    }

    void configsRoundTrip()
    {
        for (const QString &category : kCategories) {
            TagConfig c = defaultConfig(category);
            c.abbreviatedPhrases = keys({"cargo", "intro"});
            c.shortenedSizes = keys({"Small"});
            c.standardizeHaulingNames = true;
            const auto back = TagConfig::fromJson(c.toJson());
            QVERIFY(back);
            QVERIFY(*back == c);
        }
        QVERIFY(!TagConfig::fromJson(QStringLiteral("{nope")));
    }

    void fingerprintMatchesPython()
    {
        QMap<QString, TagConfig> all;
        for (const QString &category : kCategories)
            all.insert(category, defaultConfig(category));
        QCOMPARE(fingerprint(all, true), expected_.value(QStringLiteral("fingerprint_true")).toString());
        QCOMPARE(fingerprint(all, false), expected_.value(QStringLiteral("fingerprint_false")).toString());
    }

    void renderingMatchesPython()
    {
        const QJsonObject r = expected_.value(QStringLiteral("render")).toObject();
        const auto v = values({{"class", "Military"}, {"size", "2"}, {"grade", "A"}, {"type", "Cooler"}});
        TagConfig comp = defaultConfig(QStringLiteral("components"));
        QCOMPARE(renderTag(comp, v), r.value(QStringLiteral("components_default")).toString());
        comp.elements[3].enabled = true;
        comp.separator = QStringLiteral("pipe");
        comp.enclosing = QStringLiteral("round");
        comp.elements[0].style = QStringLiteral("long");
        comp.elements[1].style = QStringLiteral("size_n");
        comp.elements[2].style = QStringLiteral("grade_letter");
        QCOMPARE(renderTag(comp, v), r.value(QStringLiteral("components_custom")).toString());
        QCOMPARE(renderTag(comp, values({{"class", "Unknownish"}})), r.value(QStringLiteral("components_unknown")).toString());

        const TagConfig mis = defaultConfig(QStringLiteral("missiles"));
        QCOMPARE(renderTag(mis, values({{"ordinance", ""}, {"size", "2"}})), r.value(QStringLiteral("missile_bomb")).toString());
        QCOMPARE(renderTag(mis, values({{"ordinance", "Infrared"}, {"size", "1"}})),
                 r.value(QStringLiteral("missile_ir")).toString());

        TagConfig com = defaultConfig(QStringLiteral("commodities"));
        for (ElementSpec &e : com.elements)
            e.enabled = true;
        QHash<QString, QString> cv = values({{"label", "Crafting"}, {"collection", "Collection"}});
        cv.insert(QStringLiteral("usage"), QStringLiteral("Quantum Drive") + kUsageInputSep + QStringLiteral("Shield") +
                                               kUsageInputSep + QStringLiteral("Bogus"));
        QCOMPARE(renderTag(com, cv), r.value(QStringLiteral("commodity_all")).toString());
        QCOMPARE(renderTag(com, {}), r.value(QStringLiteral("commodity_empty")).toString());
    }

    void titleShorteningMatchesPython()
    {
        const QStringList titles = {
            QStringLiteral("Rookie Rank - Small Cargo Haul"),
            QStringLiteral("~mission(ReputationRank) Rank - Direct ~mission(CargoGradeToken) Cargo Haul Circuit"),
            QStringLiteral("Ling Family ~mission(ReputationRank) Cargo Haul - ~mission(CargoGradeToken) Scale"),
            QStringLiteral("~mission(ReputationRank) Hauler Needed for ~mission(CargoGradeToken) Shipment"),
            QStringLiteral("Opportunity for Independent Cargo Hauler -"),
            QStringLiteral("Direct Local Shipment Route, Rank, x"),
        };
        struct Opt
        {
            QSet<QString> enabled;
            QString sep;
            bool standardize;
        };
        const QList<Opt> options = {
            {{}, QStringLiteral("dash"), false},
            {keys({"rank", "cargo", "haul"}), QStringLiteral("pipe"), false},
            {keys({"intro", "hauler_needed_for", "local_shipment_route", "ling_family_rank", "ling_family_prefix",
                   "underline_direct"}),
             QStringLiteral("colon"), false},
            {keys({"rank"}), QStringLiteral("space"), true},
            {keys({"cargo"}), QStringLiteral("dash"), true},
        };
        const QJsonArray expected = expected_.value(QStringLiteral("abbreviate")).toArray();
        QCOMPARE(expected.size(), titles.size());
        for (qsizetype t = 0; t < titles.size(); ++t)
            for (qsizetype o = 0; o < options.size(); ++o)
                QCOMPARE(abbreviateTitle(titles[t], options[o].enabled, options[o].sep, options[o].standardize),
                         expected[t].toArray()[o].toString());
    }

    void routesMatchPython()
    {
        const QJsonArray r = expected_.value(QStringLiteral("route")).toArray();
        QCOMPARE(renderRoute(QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("shape"), true, false), r[0].toString());
        QCOMPARE(renderRoute(QStringLiteral("A"), {}, QStringLiteral("gt")), r[1].toString());
        QCOMPARE(renderRoute({}, QStringLiteral("B"), QStringLiteral("to")), r[2].toString());
        QCOMPARE(renderRoute(QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("arrow")), r[3].toString());

        TagConfig t = defaultConfig(QStringLiteral("mission_titles"));
        QVERIFY(routeEnabled(&t));
        QCOMPARE(applyMissionTitle(QStringLiteral("Title"), QStringLiteral("A > B"), t), QStringLiteral("Title - A > B"));
        t.placement = QStringLiteral("prepend");
        t.titleSeparator = QStringLiteral("colon");
        QCOMPARE(applyMissionTitle(QStringLiteral("Title"), QStringLiteral("A > B"), t), QStringLiteral("A > B: Title"));
        t.placement = QStringLiteral("replace");
        QCOMPARE(applyMissionTitle(QStringLiteral("Title"), QStringLiteral("A > B"), t), QStringLiteral("A > B"));
        QCOMPARE(applyMissionTitle(QStringLiteral("Title"), {}, t), QStringLiteral("Title"));
        t.elements[0].enabled = false;
        QVERIFY(!routeEnabled(&t));
        QVERIFY(!routeEnabled(nullptr));
    }

    void legacyBlobsMatchPython()
    {
        const QJsonObject blob = QJsonDocument::fromJson(R"({"elements": [{"kind": "class", "enabled": true, "style": "long"},
            {"kind": "size"}], "class_mapping": {"X": ["a", "b"]}, "abbreviate_title": true, "placement": "weird",
            "rank_separator": "zz"})").object();
        QCOMPARE(TagConfig::fromObject(blob).toJson(), expected_.value(QStringLiteral("legacy_from_dict")).toString());
    }

    void joinTagPlacement()
    {
        QCOMPARE(joinTag(QStringLiteral("Name"), QStringLiteral("[T]"), QStringLiteral("append")), QStringLiteral("Name [T]"));
        QCOMPARE(joinTag(QStringLiteral("Name"), QStringLiteral("[T]"), QStringLiteral("prepend")), QStringLiteral("[T] Name"));
        QCOMPARE(joinTag(QStringLiteral("Name"), QStringLiteral("[T]"), QStringLiteral("odd")), QStringLiteral("[T] Name"));
        QCOMPARE(joinTag(QStringLiteral("Name"), {}, QStringLiteral("append")), QStringLiteral("Name"));
    }

    void shipWeaponDamageKeysAreRenamed()
    {
        TagConfig c = defaultConfig(QStringLiteral("ship_weapons"));
        c.classMapping.remove(QStringLiteral("Physical"));
        c.classMapping.insert(QStringLiteral("Phys"), {QStringLiteral("X"), QStringLiteral("XX"), QStringLiteral("XXX")});
        c.classMapping.insert(QStringLiteral("Bio"), {QStringLiteral("old"), QStringLiteral("old"), QStringLiteral("old")});
        migrateMapping(QStringLiteral("ship_weapons"), c);
        QVERIFY(!c.classMapping.contains(QStringLiteral("Phys")));
        QCOMPARE(c.classMapping.value(QStringLiteral("Physical"))[0], QStringLiteral("X")); // renamed
        QVERIFY(!c.classMapping.contains(QStringLiteral("Bio")));                           // orphan dropped
        QCOMPARE(c.classMapping.value(QStringLiteral("Biochemical"))[0], QStringLiteral("B"));
    }

    void newElementsAreBackfilledInPlace()
    {
        // A pre-2.1 commodities config: no "usage" between label and collection.
        TagConfig c;
        c.elements = {{QStringLiteral("label"), true, QStringLiteral("short")},
                      {QStringLiteral("collection"), true, QStringLiteral("long")}};
        backfillNewElements(QStringLiteral("commodities"), c);
        QCOMPARE(c.elements.size(), 3);
        QCOMPARE(c.elements[1].kind, QStringLiteral("usage"));
        QVERIFY(!c.elements[1].enabled); // inherits the default (off)
        QVERIFY(c.classMapping.contains(QStringLiteral("Quantum Drive")));

        // A pre-1.4.2 components config gains "type", disabled, at the end.
        TagConfig comp = defaultConfig(QStringLiteral("components"));
        comp.elements.removeLast();
        comp.classMapping.clear();
        backfillNewElements(QStringLiteral("components"), comp);
        QCOMPARE(comp.elements.last().kind, QStringLiteral("type"));
        QVERIFY(comp.classMapping.contains(QStringLiteral("Military")));
    }
};

QTEST_GUILESS_MAIN(TestTags)
#include "tst_tags.moc"
