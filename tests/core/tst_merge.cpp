// Ports test_ini_merger.py, test_status_classification.py and the category
// cases from test_core.py / string_model.py, plus load_source_files.

#include "core/merge/Merger.h"
#include "core/merge/SourceLoader.h"
#include "core/model/Enhancements.h"
#include "core/model/StringEntry.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace core;

namespace {

IniMap ini(std::initializer_list<std::pair<const char *, const char *>> pairs)
{
    IniMap m;
    for (const auto &[k, v] : pairs)
        m.insert(QString::fromUtf8(k), QString::fromUtf8(v));
    return m;
}

QString v(const IniMap &m, const char *key)
{
    return m.value(QString::fromUtf8(key));
}

void writeFile(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        qFatal("cannot open %s", qPrintable(f.fileName()));
    f.write(bytes);
}

const StringEntry *findEntry(const QList<StringEntry> &entries, const QString &key)
{
    for (const StringEntry &e : entries)
        if (e.key == key)
            return &e;
    return nullptr;
}

} // namespace

class TestMerge : public QObject
{
    Q_OBJECT

private slots:
    // --- categories --------------------------------------------------------

    void categories_data()
    {
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("category");
        QTest::newRow("ship name") << "vehicle_NameANVL_Carrack" << "Ships";
        QTest::newRow("ship desc") << "vehicle_DescANVL_Carrack" << "Ships";
        QTest::newRow("wikelo ship mod") << "TheCollector_ShipMod_Thing_VehicleName" << "Ships";
        QTest::newRow("shield") << "item_NameSHLD_Aspirum" << "Ship Items";
        QTest::newRow("power plant") << "item_NamePOWR_TR1" << "Ship Items";
        QTest::newRow("cooler") << "item_NameCOOL_WCPR_S02_Taiga" << "Ship Items";
        QTest::newRow("turret") << "item_Name_Turret_S1" << "Ship Items";
        QTest::newRow("qdrv underscore") << "item_Name_QDRV_RSI_S02_Hemera" << "Ship Items";
        QTest::newRow("qdrv desc underscore") << "item_Desc_QDRV_RSI_S02_Hemera" << "Ship Items";
        QTest::newRow("lowercase name") << "item_nameQDRV_RSI_S02_Hemera_SCItem" << "Ship Items";
        QTest::newRow("lowercase desc") << "item_descqdrv_rsi_s02_hemera_scitem" << "Ship Items";
        QTest::newRow("XL size") << "item_Name_XL-1_something" << "Ship Items";
        QTest::newRow("XXL size") << "item_Name_XXL_something" << "Ship Items";
        QTest::newRow("fps rifle word") << "item_Name_rifle" << "Gear";
        QTest::newRow("fps weapon infix") << "item_NameKSAR_rifle_ballistic_01" << "Gear";
        QTest::newRow("armor") << "item_Name_armor_heavy_helmet" << "Gear";
        QTest::newRow("uppercase maker") << "item_NameBEHR_LaserCannon_S2" << "Ship Items";
        QTest::newRow("lowercase maker") << "item_Namebehr_glsn_01" << "Gear";
        QTest::newRow("digit maker") << "item_Name7_thing" << "Gear";
        QTest::newRow("mining gadget") << "item_mining_gadget_boremax" << "Gear";
        QTest::newRow("mining head") << "item_mining_arbor" << "Ship Items";
        QTest::newRow("commodity") << "items_commodities_laranite" << "Commodities";
        QTest::newRow("journal") << "Journal_General_Mining" << "Journal";
        QTest::newRow("contract") << "Contract_Title_X" << "Missions";
        QTest::newRow("shubin") << "Shubin_Mining_Title" << "Missions";
        QTest::newRow("bitzeroes spelling") << "BitZeroes_Desc" << "Missions";
        QTest::newRow("other") << "ui_SomeButton" << "Other";
        QTest::newRow("empty") << "" << "Other";
    }

    void categories()
    {
        QFETCH(QString, key);
        QFETCH(QString, category);
        QCOMPARE(extractCategory(key), category);
    }

    void favouritableShips()
    {
        StringEntry name{QStringLiteral("vehicle_NameANVL_Carrack"), {}, category::kShips, {}, {}, {}};
        StringEntry desc{QStringLiteral("vehicle_DescANVL_Carrack"), {}, category::kShips, {}, {}, {}};
        StringEntry wrongCategory{QStringLiteral("vehicle_NameX"), {}, category::kOther, {}, {}, {}};
        QVERIFY(name.isFavoritableShip());
        QVERIFY(!desc.isFavoritableShip());
        QVERIFY(!wrongCategory.isFavoritableShip());
        QVERIFY(isShipNameKey(u"TheCollector_ShipMod_A_VehicleNameShort"));
    }

    // --- sync_key_variants -------------------------------------------------

    // The 7SA pair: the short untagged sibling must not win.
    void shortUntaggedSiblingLoses()
    {
        IniMap m = ini({{"item_Name_SHLD_BEHR_S01_7SA", "[SHLD-S1-B] BEHR Shield"},
                        {"item_NameSHLD_BEHR_S01_7sa", "BEHR Shield"}});
        syncKeyVariants(m);
        QCOMPARE(v(m, "item_Name_SHLD_BEHR_S01_7SA"), QStringLiteral("[SHLD-S1-B] BEHR Shield"));
        QCOMPARE(v(m, "item_NameSHLD_BEHR_S01_7sa"), QStringLiteral("[SHLD-S1-B] BEHR Shield"));
    }

    // The Taiga pair: the _SCItem variant can be the right one.
    void scitemVariantCanWin()
    {
        IniMap m = ini({{"item_Name_COOL_WCPR_S02_Taiga", "Taiga Cooler"},
                        {"item_NameCOOL_WCPR_S02_Taiga_SCItem", "[COOL-S2-B] Taiga Cooler"}});
        syncKeyVariants(m);
        QCOMPARE(v(m, "item_Name_COOL_WCPR_S02_Taiga"), QStringLiteral("[COOL-S2-B] Taiga Cooler"));
    }

    void userEditBeatsLongerSibling()
    {
        IniMap m = ini({{"item_Name_SHLD_BEHR_S01_7SA", "My Custom Name"},
                        {"item_NameSHLD_BEHR_S01_7sa", "[SHLD-S1-B] BEHR Shield Original"}});
        IniMap untouched = m;
        syncKeyVariants(m, {QStringLiteral("item_Name_SHLD_BEHR_S01_7SA")});
        QCOMPARE(v(m, "item_NameSHLD_BEHR_S01_7sa"), QStringLiteral("My Custom Name"));
        syncKeyVariants(untouched);
        QCOMPARE(v(untouched, "item_Name_SHLD_BEHR_S01_7SA"),
                 QStringLiteral("[SHLD-S1-B] BEHR Shield Original"));
    }

    void longestOfSeveralUserEdits()
    {
        IniMap m = ini({{"item_Name_SHLD_BEHR_S01_7SA", "Short Edit"},
                        {"item_NameSHLD_BEHR_S01_7sa", "A Longer Deliberate Edit"}});
        syncKeyVariants(
            m, {QStringLiteral("item_Name_SHLD_BEHR_S01_7SA"), QStringLiteral("item_NameSHLD_BEHR_S01_7sa")});
        QCOMPARE(v(m, "item_Name_SHLD_BEHR_S01_7SA"), QStringLiteral("A Longer Deliberate Edit"));
    }

    // Python's max() keeps the first of equal-length candidates.
    void tieKeepsFirstInOrder()
    {
        IniMap m = ini({{"item_Name_SHLD_A", "AAAA"}, {"item_NameSHLDa", "BBBB"}});
        syncKeyVariants(m);
        QCOMPARE(v(m, "item_NameSHLDa"), QStringLiteral("AAAA"));
    }

    // #255: only item_Name*/item_Desc* keys take part.
    void scopeGuards_data()
    {
        QTest::addColumn<QString>("keyA");
        QTest::addColumn<QString>("valueA");
        QTest::addColumn<QString>("keyB");
        QTest::addColumn<QString>("valueB");
        QTest::newRow("crusader") << "Stanton2" << "Crusader" << "Stanton_2" << "Stanton (Star)";
        QTest::newRow("comm array") << "CommArray_Deactivate" << "Deactivate" << "comm_Array_Deactivate"
                                    << "Disconnect Uplink";
        QTest::newRow("thrusters") << "port_NameThrusterMavML" << "Thruster Mid Left"
                                   << "port_NameThrusterMavML_"
                                   << "Thruster Mid Lower";
        QTest::newRow("hardpoints") << "itemPort_hardpoint_power_plant_02" << "Power Plant"
                                    << "itemPort_hardpoint_powerplant_02" << "Power Plant - 02";
        QTest::newRow("kiosk") << "kiosk_ShopTerminal" << "Shop Terminal" << "kiosk_Shop_Terminal"
                               << "Shop_Terminal";
    }

    void scopeGuards()
    {
        QFETCH(QString, keyA);
        QFETCH(QString, valueA);
        QFETCH(QString, keyB);
        QFETCH(QString, valueB);
        SourceMap sources;
        sources[kSourceGlobal].insert(keyA, valueA);
        sources[kSourceGlobal].insert(keyB, valueB);
        const IniMap merged = mergeSourcesByHierarchy(sources, {kSourceGlobal});
        QCOMPARE(merged.value(keyA), valueA);
        QCOMPARE(merged.value(keyB), valueB);
    }

    void itemDescVariantsStillSync()
    {
        IniMap m = ini({{"item_Desc_SHLD_BEHR_S01_7SA", "[SHLD-S1-B] BEHR Shield description"},
                        {"item_DescSHLD_BEHR_S01_7sa", "BEHR Shield description"}});
        syncKeyVariants(m);
        QCOMPARE(v(m, "item_DescSHLD_BEHR_S01_7sa"), QStringLiteral("[SHLD-S1-B] BEHR Shield description"));
    }

    void canonicalKeys()
    {
        // Expected values computed with the original Python _get_canonical_key.
        QCOMPARE(canonicalItemKey(QStringLiteral("item_Name_QDRV_RSI_S02_Hemera")),
                 QStringLiteral("itemname_qdrvrsis02hemera"));
        QCOMPARE(canonicalItemKey(QStringLiteral("item_nameQDRV_RSI_S02_Hemera_SCItem")),
                 QStringLiteral("itemname_qdrvrsis02hemera"));
        // "gmisl" is replaced after "misl", which has already split it.
        QCOMPARE(canonicalItemKey(QStringLiteral("item_Name_GMISL_X")), QStringLiteral("itemnameg_mislx"));
        QCOMPARE(canonicalItemKey(QStringLiteral("item_Name_Plain")), QStringLiteral("itemnameplain"));
        QCOMPARE(canonicalItemKey(QStringLiteral("item_NameSHLDa")), QStringLiteral("itemname_shlda"));
    }

    // --- merge_sources_by_hierarchy -------------------------------------------

    void laterSourcesWinUserLast()
    {
        SourceMap sources;
        sources[QStringLiteral("global")] = ini({{"key1", "base_val"}, {"key2", "val2"}});
        sources[QStringLiteral("contracts")] = ini({{"key1", "override_val"}, {"key3", "val3"}});
        const IniMap user = ini({{"key1", "user_val"}});
        const IniMap r = mergeSourcesByHierarchy(
            sources, {QStringLiteral("global"), QStringLiteral("contracts"), QStringLiteral("missing")},
            &user);
        QCOMPARE(v(r, "key1"), QStringLiteral("user_val"));
        QCOMPARE(v(r, "key2"), QStringLiteral("val2"));
        QCOMPARE(v(r, "key3"), QStringLiteral("val3"));
        QCOMPARE(r.entries().first().first, QStringLiteral("key1")); // first source's order leads
    }

    void userOverrideToShortVariantSurvivesMerge()
    {
        SourceMap sources;
        sources[kSourceGlobal] = ini({{"item_Name_SHLD_BEHR_S01_7SA", "[SHLD-S1-B] BEHR Shield"},
                                      {"item_NameSHLD_BEHR_S01_7sa", "BEHR Shield"}});
        const IniMap user = ini({{"item_Name_SHLD_BEHR_S01_7SA", "My Renamed Shield"}});
        const IniMap r = mergeSourcesByHierarchy(sources, {kSourceGlobal}, &user);
        QCOMPARE(v(r, "item_NameSHLD_BEHR_S01_7sa"), QStringLiteral("My Renamed Shield"));
    }

    // --- status ---------------------------------------------------------------

    void status()
    {
        const QString g = kSourceGlobal;
        QCOMPARE(statusFromSource(kSourceUser, g, true, true), EntryStatus::Modified);
        QCOMPARE(statusFromSource(kSourceEnhancements, g, true, true), EntryStatus::Enhanced);
        QCOMPARE(statusFromSource(g, g, true, true), EntryStatus::Unmodified);
        QCOMPARE(statusFromSource(QStringLiteral("custom_base"), QStringLiteral("custom_base"), true, true),
                 EntryStatus::Unmodified);
        QCOMPARE(statusFromSource(QStringLiteral("contracts"), g, true, true), EntryStatus::Modified);
        QCOMPARE(statusFromSource(kSourceUser, g, false, true), EntryStatus::New);
        QCOMPARE(statusFromSource(kSourceEnhancements, g, false, true), EntryStatus::New);
        QCOMPARE(statusFromSource(kSourceEnhancements, g, true, false), EntryStatus::New);
    }

    // --- load_source_files ----------------------------------------------------

    void buildsEntriesWithStatusAndCategory()
    {
        LoadedSources loaded;
        loaded.sources[kSourceGlobal] = ini({{"vehicle_NameANVL_Carrack", "Carrack"},
                                             {"vehicle_NameANVL_Carrack_short", "Carr"},
                                             {"item_NameSHLD_Aspirum", "Aspirum"},
                                             {"ui_Button", "OK"},
                                             {"Journal_Entry", "Text"}});
        loaded.sources[kSourceEnhancements] =
            ini({{"item_NameSHLD_Aspirum", "[SHLD] Aspirum"}, {"discovered_key", "From XML"}});
        loaded.sources[kSourceUser] = ini({{"ui_Button", "Okay"}, {"user_only", " spaced"}});
        loaded.hierarchy = {kSourceGlobal, kSourceEnhancements, kSourceUser};
        loaded.enhancementCategories.insert(QStringLiteral("discovered_key"), category::kGear);

        const QList<StringEntry> entries = buildEntries(loaded);
        QVERIFY(!findEntry(entries, QStringLiteral("vehicle_NameANVL_Carrack_short"))); // short names skipped

        const StringEntry *ship = findEntry(entries, QStringLiteral("vehicle_NameANVL_Carrack"));
        QVERIFY(ship);
        QCOMPARE(ship->status, EntryStatus::Unmodified);
        QCOMPARE(ship->category, category::kShips);
        QCOMPARE(ship->sourceFile, kSourceGlobal);

        const StringEntry *shield = findEntry(entries, QStringLiteral("item_NameSHLD_Aspirum"));
        QCOMPARE(shield->status, EntryStatus::Enhanced);
        QCOMPARE(shield->originalValue, QStringLiteral("[SHLD] Aspirum"));
        QCOMPARE(shield->sourceFile, kSourceEnhancements);

        const StringEntry *discovered = findEntry(entries, QStringLiteral("discovered_key"));
        QCOMPARE(discovered->status, EntryStatus::New);
        QCOMPARE(discovered->category, category::kGear);

        const StringEntry *button = findEntry(entries, QStringLiteral("ui_Button"));
        QCOMPARE(button->status, EntryStatus::Modified);
        QCOMPARE(button->originalValue, QStringLiteral("OK"));
        QCOMPARE(button->customValue, QStringLiteral("Okay"));
        QVERIFY(button->isModified());

        const StringEntry *userOnly = findEntry(entries, QStringLiteral("user_only"));
        QCOMPARE(userOnly->status, EntryStatus::New);
        QCOMPARE(userOnly->sourceFile, kSourceUser);
        QCOMPARE(userOnly->customValue, QStringLiteral(" spaced"));

        QCOMPARE(findEntry(entries, QStringLiteral("Journal_Entry"))->category, category::kJournal);
        QCOMPARE(entries.size(), 6);
    }

    void loadsSourcesFromDisk()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("base.ini")), "\xEF\xBB\xBF"
                                                            "a=1\r\nitem_NameSHLD_X=Shield\r\n");
        writeFile(dir.filePath(QStringLiteral("components_desc_enhancements.ini")),
                  "item_NameSHLD_X=[S] Shield\r\n");
        writeFile(dir.filePath(QStringLiteral("ships_desc_enhancements.ini")), "vehicle_DescX=Stats\r\n");
        writeFile(dir.filePath(QStringLiteral("user.ini")), "a= keep space\r\n");

        SourceFiles files;
        files.baseIni = dir.filePath(QStringLiteral("base.ini"));
        files.enhancementsDir = dir.path();
        files.enhancementFileIds = {QStringLiteral("component_descs")}; // ships disabled
        files.userIni = dir.filePath(QStringLiteral("user.ini"));

        const LoadedSources loaded = loadSources(files);
        QVERIFY(loaded.problems.isEmpty());
        QCOMPARE(loaded.hierarchy, (QStringList{kSourceGlobal, kSourceEnhancements, kSourceUser}));
        QCOMPARE(v(loaded.sources.at(kSourceGlobal), "a"), QStringLiteral("1"));
        QCOMPARE(v(loaded.sources.at(kSourceEnhancements), "item_NameSHLD_X"), QStringLiteral("[S] Shield"));
        QVERIFY(!loaded.sources.at(kSourceEnhancements).contains(QStringLiteral("vehicle_DescX")));
        QCOMPARE(loaded.enhancementCategories.value(QStringLiteral("item_NameSHLD_X")), category::kShipItems);
        QCOMPARE(v(loaded.sources.at(kSourceUser), "a"), QStringLiteral(" keep space"));

        files.baseIni = dir.filePath(QStringLiteral("missing.ini"));
        QCOMPARE(loadSources(files).problems.size(), 1);
    }

    void enhancementCatalog()
    {
        QCOMPARE(enhancements::files().size(), std::size_t(9));
        QCOMPARE(enhancements::categoryLabelForFile(QStringLiteral("missile_enhancements")),
                 category::kShipItems);
        QCOMPARE(enhancements::fileNameFor(QStringLiteral("journal")),
                 QStringLiteral("journal_enhancements.ini"));
        QVERIFY(enhancements::categoryLabelForFile(QStringLiteral("nope")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestMerge)
#include "tst_merge.moc"
