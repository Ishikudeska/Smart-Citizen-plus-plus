// The String Editor's table logic. Ports test_ship_sort_prefix.py,
// test_entry_filter.py, test_favorite_toggle_stranded.py and
// test_onedrive.py.

#include "core/model/StringTable.h"
#include "core/util/OneDrive.h"

#include <QTest>

using namespace core;
using namespace core::table;

namespace {

const QString kFav = QStringLiteral("*");

StringEntry entry(const QString &key, const QString &category, const QString &original, const QString &custom = {},
                  EntryStatus status = EntryStatus::Unmodified)
{
    StringEntry e;
    e.key = key;
    e.category = category;
    e.originalValue = original;
    e.customValue = custom;
    e.status = status;
    e.sourceFile = QStringLiteral("global");
    return e;
}

QProcessEnvironment env(std::initializer_list<std::pair<const char *, const char *>> vars)
{
    QProcessEnvironment e;
    for (const auto &[k, v] : vars)
        e.insert(QString::fromLatin1(k), QString::fromLatin1(v));
    return e;
}

} // namespace

class TestStringTable : public QObject
{
    Q_OBJECT

private slots:
    void readsSortOrder()
    {
        QCOMPARE(sortOrder(QStringLiteral("Avenger"), kFav), QString());
        QCOMPARE(sortOrder(QStringLiteral("01-Avenger"), kFav), QStringLiteral("01"));
        QCOMPARE(sortOrder(QStringLiteral("*Avenger"), kFav), QString());
        QCOMPARE(sortOrder(QStringLiteral("*01-Avenger"), kFav), QStringLiteral("01"));
        QCOMPARE(sortOrder(QStringLiteral("1-Avenger"), kFav), QString());
        QCOMPARE(sortOrder(QStringLiteral("05Avenger"), kFav), QString());
        QCOMPARE(sortOrder(QString(), kFav), QString());
        QCOMPARE(sortOrder(QStringLiteral("*99-Reclaimer"), kFav), QStringLiteral("99"));
        for (const char *name : {"300i", "600i", "890 Jump", "100i", "85X", "125a"}) {
            QCOMPARE(sortOrder(QString::fromLatin1(name), kFav), QString());
            QCOMPARE(sortOrder(kFav + QString::fromLatin1(name), kFav), QString());
        }
    }

    void setsSortOrder()
    {
        const auto set = [](const char *custom, const char *original, const char *order) {
            return withSortOrder(QString::fromUtf8(custom), QString::fromUtf8(original), kFav, QString::fromUtf8(order));
        };
        QCOMPARE(set("", "Avenger", "05"), QStringLiteral("05-Avenger"));
        QCOMPARE(set("Avenger Titan", "Avenger", "02"), QStringLiteral("02-Avenger Titan"));
        QCOMPARE(set("03-Avenger", "Avenger", "07"), QStringLiteral("07-Avenger"));
        QCOMPARE(set("03-Avenger", "Avenger", ""), QString());
        QCOMPARE(set("03-Avenger Titan", "Avenger", ""), QStringLiteral("Avenger Titan"));
        QCOMPARE(set("*300i", "300i", ""), QStringLiteral("*300i"));
        QCOMPARE(set("300i", "300i", ""), QString());
        QCOMPARE(set("300i", "300i", "05"), QStringLiteral("05-300i"));
        QCOMPARE(set("*300i", "300i", "05"), QStringLiteral("*05-300i"));
        QCOMPARE(set("890 Jump", "890 Jump", "03"), QStringLiteral("03-890 Jump"));
        QCOMPARE(set("*05-300i", "300i", ""), QStringLiteral("*300i"));
        QCOMPARE(set("*Avenger", "Avenger", "01"), QStringLiteral("*01-Avenger"));
        QCOMPARE(set("*01-Avenger", "Avenger", "08"), QStringLiteral("*08-Avenger"));
        QCOMPARE(set("*01-Avenger", "Avenger", ""), QStringLiteral("*Avenger"));
        QCOMPARE(set("*Avenger", "Avenger", ""), QStringLiteral("*Avenger"));
        QCOMPARE(set("Avenger", "Avenger", "5"), QStringLiteral("05-Avenger"));
        QCOMPARE(set("Avenger", "Avenger", "10"), QStringLiteral("10-Avenger"));
    }

    void togglesFavorites()
    {
        StringEntry name = entry(QStringLiteral("vehicle_NameAEGS_Avenger"), category::kShips, QStringLiteral("Avenger"));
        QVERIFY(toggleFavorite(name, kFav));
        QCOMPARE(name.customValue, QStringLiteral("*Avenger"));
        QCOMPARE(name.status, EntryStatus::Modified);
        QVERIFY(toggleFavorite(name, kFav));
        QCOMPARE(name.customValue, QString());
        QCOMPARE(name.status, EntryStatus::Unmodified);

        // A description can't be starred, but a stranded prefix comes off.
        StringEntry desc = entry(QStringLiteral("vehicle_DescAEGS_Avenger"), category::kShips, QStringLiteral("Desc"));
        QVERIFY(!toggleFavorite(desc, kFav));
        desc.customValue = QStringLiteral("*Desc");
        QVERIFY(toggleFavorite(desc, kFav));
        QCOMPARE(desc.customValue, QString());
        desc.customValue = QStringLiteral("*Better desc");
        QVERIFY(toggleFavorite(desc, kFav));
        QCOMPARE(desc.customValue, QStringLiteral("Better desc"));
        StringEntry other = entry(QStringLiteral("ui_x"), category::kOther, QStringLiteral("x"), QStringLiteral("*x"));
        QVERIFY(toggleFavorite(other, kFav));
        QCOMPARE(other.customValue, QString());
        // An empty prefix never touches non-name rows.
        StringEntry plain = entry(QStringLiteral("ui_y"), category::kOther, QStringLiteral("y"));
        QVERIFY(!toggleFavorite(plain, QString()));
    }

    void filters()
    {
        QList<StringEntry> entries = {
            entry(QStringLiteral("vehicle_NameAEGS_Avenger"), category::kShips, QStringLiteral("Avenger"),
                  QStringLiteral("*02-Avenger"), EntryStatus::Modified),
            entry(QStringLiteral("vehicle_DescAEGS_Avenger"), category::kShips, QStringLiteral("A ship"),
                  QStringLiteral("*A ship"), EntryStatus::Modified),
            entry(QStringLiteral("item_NameShield"), category::kShipItems, QStringLiteral("Shield"), {},
                  EntryStatus::Enhanced),
            entry(QStringLiteral("Mission_title"), category::kMissions, QStringLiteral("Haul [BP]")),
            entry(QStringLiteral("Mission_desc"), category::kMissions,
                  QStringLiteral(R"(Text\n\n<EM4>POTENTIAL BLUEPRINTS</EM4>\n- Thing)")),
            entry(QStringLiteral("Commodity_desc"), category::kCommodities,
                  QStringLiteral(R"(<EM4>POTENTIAL BLUEPRINTS</EM4>\n- Thing)")),
            entry(QStringLiteral("TheCollector_ShipMod_MISC_Fortune_VehicleName"), category::kShips,
                  QStringLiteral("Fortune")),
        };
        IniMap defaults;
        defaults.insert(QStringLiteral("item_NameShield"), QStringLiteral("Stock Shield"));

        FilterCriteria c;
        QCOMPARE(filterEntryIndices(entries, defaults, c).size(), entries.size());
        QVERIFY(filterEntryIndices({}, defaults, c).isEmpty());

        FilterCriteria hide;
        hide.hideUnmodified = true;
        QCOMPARE(filterEntryIndices(entries, defaults, hide), (QList<int>{0, 1, 2}));

        FilterCriteria cat;
        cat.category = category::kShipItems;
        QCOMPARE(filterEntryIndices(entries, defaults, cat), QList<int>{2});

        FilterCriteria status;
        status.status = QStringLiteral("Enhanced");
        QCOMPARE(filterEntryIndices(entries, defaults, status), QList<int>{2});

        FilterCriteria fav;
        fav.favoritesOnly = true; // the starred description row doesn't count
        QCOMPARE(filterEntryIndices(entries, defaults, fav), QList<int>{0});

        FilterCriteria col;
        col.columnText[ColDefault] = QStringLiteral("stock");
        QCOMPARE(filterEntryIndices(entries, defaults, col), QList<int>{2});
        col = {};
        col.columnText[ColOrder] = QStringLiteral("02");
        QCOMPARE(filterEntryIndices(entries, defaults, col), QList<int>{0});
        col = {};
        col.columnText[ColStar] = QStringLiteral("\u2605");
        QCOMPARE(filterEntryIndices(entries, defaults, col), QList<int>{0});
        col = {};
        col.columnText[ColStatus] = QStringLiteral("enh");
        QCOMPARE(filterEntryIndices(entries, defaults, col), QList<int>{2});

        FilterCriteria bp;
        bp.bpTitlesOnly = true;
        QCOMPARE(filterEntryIndices(entries, defaults, bp), QList<int>{3});
        bp.bpTitlesOnly = false;
        bp.bpDescsOnly = true; // the commodity's colliding header is not a mission
        QCOMPARE(filterEntryIndices(entries, defaults, bp), QList<int>{4});
        bp.bpTitlesOnly = true;
        QCOMPARE(filterEntryIndices(entries, defaults, bp), (QList<int>{3, 4}));

        FilterCriteria names;
        names.shipVehicleNamesOnly = true;
        QCOMPARE(filterEntryIndices(entries, defaults, names), (QList<int>{0, 6}));
    }

    void groupsKeys()
    {
        QCOMPARE(groupSortKey(QStringLiteral("item_NameShield")), std::make_pair(QStringLiteral("item_shield"), 0));
        QCOMPARE(groupSortKey(QStringLiteral("item_DescShield")), std::make_pair(QStringLiteral("item_shield"), 1));
        QCOMPARE(groupSortKey(QStringLiteral("vehicle_DescX")), std::make_pair(QStringLiteral("vehicle_x"), 1));
        QCOMPARE(groupSortKey(QStringLiteral("items_commodities_gold")),
                 std::make_pair(QStringLiteral("items_commodities_gold"), 0));
        QCOMPARE(groupSortKey(QStringLiteral("items_commodities_gold_desc")),
                 std::make_pair(QStringLiteral("items_commodities_gold"), 1));
        QCOMPARE(groupSortKey(QStringLiteral("Bounty_title_001")), std::make_pair(QStringLiteral("bounty_001"), 0));
        QCOMPARE(groupSortKey(QStringLiteral("Bounty_desc_001")), std::make_pair(QStringLiteral("bounty_001"), 1));
        QCOMPARE(groupSortKey(QStringLiteral("ui_Thing")), std::make_pair(QStringLiteral("ui_thing"), 0));
    }

    void sortsStably()
    {
        QList<StringEntry> entries = {
            entry(QStringLiteral("b"), category::kOther, QStringLiteral("x")),
            entry(QStringLiteral("A"), category::kOther, QStringLiteral("x")),
            entry(QStringLiteral("vehicle_NameZ"), category::kShips, QStringLiteral("Z"), QStringLiteral("*01-Z")),
        };
        QList<int> rows = {0, 1, 2};
        sortIndices(rows, entries, {}, ColKey, false, false, kFav, {});
        QCOMPARE(rows, (QList<int>{1, 0, 2}));
        sortIndices(rows, entries, {}, ColCurrent, true, false, kFav, {}); // ties keep their order
        QCOMPARE(rows, (QList<int>{2, 1, 0}));
        sortIndices(rows, entries, {}, ColOrder, false, false, kFav, {});
        QCOMPARE(rows.front(), 2);
    }

    void rendersPreview()
    {
        const QString html = previewHtml(QStringLiteral("k"), QStringLiteral(R"(<EM4>Hi</EM4>\n~mission(Location|Address) & <EM3>u</EM3>)"));
        QVERIFY(html.contains(QStringLiteral("<span style=\"font-weight:bold;color:#4a9eff;\">Hi</span><br>")));
        QVERIFY(html.contains(QStringLiteral("[Location]")));
        QVERIFY(html.contains(QStringLiteral("&amp;")));
        QVERIFY(html.contains(QStringLiteral("text-decoration:underline;\">u</span>")));
        QVERIFY(previewHtml(QStringLiteral("k"), {}).contains(QStringLiteral("(empty)")));

        StringEntry journal = entry(QStringLiteral("Journal_Thing_Content"), category::kJournal, QStringLiteral("x"));
        QCOMPARE(journalStampFor(journal, QStringLiteral("SCX"), QStringLiteral("1.0")), QString());
        journal.customValue = QStringLiteral("y");
        QCOMPARE(journalStampFor(journal, QStringLiteral("SCX"), QStringLiteral("1.0")),
                 QStringLiteral("[Edited with SCX v1.0]"));
        journal.key = QStringLiteral("Journal_Thing_Title");
        QCOMPARE(journalStampFor(journal, QStringLiteral("SCX"), QStringLiteral("1.0")), QString());
    }

    void keepsPendingEdits()
    {
        QList<StringEntry> before = {entry(QStringLiteral("a"), category::kOther, QStringLiteral("x"), QStringLiteral("y"))};
        QList<StringEntry> after = {entry(QStringLiteral("a"), category::kOther, QStringLiteral("y"))};
        QCOMPARE(restorePendingEdits(after, pendingEdits(before)), 1);
        QCOMPARE(after[0].status, EntryStatus::Unmodified); // the new original already says "y"
    }

    void detectsOneDrive()
    {
        const auto e = env({{"OneDrive", R"(C:\Users\aabou\OneDrive)"}});
        QVERIFY(onedrive::isOneDrivePath(QStringLiteral(R"(C:\Users\aabou\OneDrive\Documents\Smart Citizen)"), e));
        QVERIFY(onedrive::isOneDrivePath(QStringLiteral(R"(C:\Users\aabou\OneDrive)"), e));
        QVERIFY(!onedrive::isOneDrivePath(QStringLiteral(R"(C:\Users\aabou\Documents\Smart Citizen)"), e));
        QVERIFY(!onedrive::isOneDrivePath(QStringLiteral(R"(C:\SmartCitizenData)"), e));
        QVERIFY(onedrive::isOneDrivePath(QStringLiteral(R"(C:\Users\bob\OneDrive\Documents\SC)"), {}));
        QVERIFY(onedrive::isOneDrivePath(QStringLiteral(R"(C:\Users\bob\OneDrive - Contoso\SC)"), {}));
        QVERIFY(onedrive::isOneDrivePath(QStringLiteral(R"(C:\Users\bob\OneDrive-Contoso\SC)"), {}));
        QVERIFY(!onedrive::isOneDrivePath(QStringLiteral(R"(C:\Users\bob\OneDriveBackups\SC)"), {}));
        QVERIFY(onedrive::isOneDrivePath(QStringLiteral(R"(c:\users\aabou\onedrive\documents\sc)"), e));
        QVERIFY(!onedrive::isOneDrivePath(QString(), e));
        QVERIFY(!onedrive::isOneDrivePath(QStringLiteral(R"(C:\Users\bob\OneDriveStuff\x)"),
                                          env({{"OneDrive", R"(C:\Users\bob\OneDrive)"}})));
        QCOMPARE(onedrive::roots(env({{"OneDrive", "C:\\a\\OneDrive"}, {"OneDriveConsumer", "C:\\a\\OneDrive"}})).size(), 1);
        QVERIFY(onedrive::roots({}).isEmpty());
        const auto profile = env({{"USERPROFILE", R"(C:\Users\aabou)"}});
        QCOMPARE(onedrive::suggestLocalDataDir(QStringLiteral("Smart Citizen"), profile),
                 QStringLiteral(R"(C:\Users\aabou\Documents\Smart Citizen)"));
    }
};

QTEST_GUILESS_MAIN(TestStringTable)
#include "tst_stringtable.moc"
