// Blueprint Tracker core: the log scanner, owned-item matching, metadata
// and export. Ports test_blueprint_log_scanner.py, test_owned_items.py,
// test_blueprint_meta.py, test_blueprint_export.py,
// test_foreign_editor_blueprint_names.py and test_blueprint_scan_channels.py,
// and replays tools/parity/gen_blueprint_fixtures.py over the Kraken
// global.ini (tests/fixtures/blueprints.json).

#include "core/Settings.h"
#include "core/blueprints/BlueprintExport.h"
#include "core/blueprints/BlueprintMeta.h"
#include "core/blueprints/LogScanner.h"
#include "core/blueprints/OwnedItems.h"
#include "core/text/IniFile.h"
#include "core/text/PyText.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace core;
using namespace core::blueprints;

namespace {

QString s(const char *utf8)
{
    return QString::fromUtf8(utf8);
}

QSet<QString> set(std::initializer_list<const char *> items)
{
    QSet<QString> out;
    for (const char *i : items)
        out.insert(s(i));
    return out;
}

StringEntry entry(const char *key, const QString &value, const QString &category = category::kOther)
{
    StringEntry e;
    e.key = s(key);
    e.originalValue = value;
    e.category = category;
    return e;
}

QDateTime dt(const char *iso)
{
    return QDateTime::fromString(s(iso), Qt::ISODateWithMs).toUTC();
}

// One authoritative received-blueprint line, as the game writes it.
QString logEvent(const char *ts, const QString &name, int id = 1)
{
    return QStringLiteral("<%1> [Notice] <SHUDEvent_OnNotification> Added notification "
                          "\"Received Blueprint: %2: \" [%3] to queue. New queue size: 2, "
                          "MissionId: [00000000-0000-0000-0000-000000000000], ObjectiveId: [] "
                          "[Team_CoreGameplayFeatures][Missions][Comms]")
        .arg(s(ts), name)
        .arg(id);
}

QString writeLog(const QString &path, const QStringList &lines, const QDateTime &mtime = {})
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        qFatal("cannot write %s", qPrintable(path));
    f.write((lines.join(u'\n') + u'\n').toUtf8());
    f.flush();
    if (mtime.isValid())
        f.setFileTime(mtime, QFileDevice::FileModificationTime);
    return path;
}

QStringList fileNames(const QStringList &paths)
{
    QStringList names;
    for (const QString &p : paths)
        names << QFileInfo(p).fileName();
    return names;
}

const Enclosings kRound = {{s("("), s(")")}};
const Enclosings kSquareAndNone = {{s("["), s("]")}, kNoneStyleEnclosing};

// A blueprint-bearing mission body (literal \n separators).
const QString kDesc = s("POSTING: salvage run.\\n\\n<EM3>POTENTIAL BLUEPRINTS</EM3>"
                        "\\n- Antium Core\\n- [Mil-S1-A] Norfield\\n- Abrade Scraper Module"
                        "\\n\\n<EM3>MISSION DETAILS</EM3>\\n<EM4>Difficulty:</EM4> 3");

// A stray prose bullet before the header, region sub-headers inside the
// section, and a later section's bullet.
const QString kDescWithFalsePositives = s("Handle this right and I'll give the main contractor Council Scrip."
                                          "\\n\\n- Stows"
                                          "\\n\\n<EM3>POTENTIAL BLUEPRINTS</EM3>"
                                          "\\n<EM4>[Nyx]</EM4>\\n- Cryo-Star SL\\n- Kelvid"
                                          "\\n\\n<EM4>[Pyro]</EM4>\\n- DuraJet\\n- ZapJet"
                                          "\\n\\n<EM3>ITEM REWARDS</EM3>\\n- Council Scrip"
                                          "\\n\\n<EM3>MISSION DETAILS</EM3>\\n<EM4>Difficulty:</EM4> 3");

QString krakenPath()
{
    return QStringLiteral(SC_SOURCE_DIR "/Smart Citizen CPLusPLus/tests/fixtures/kraken_global_latest.ini");
}

QString digest(const QStringList &lines)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(lines.join(u'\n').toUtf8(), QCryptographicHash::Sha256).toHex());
}

QStringList pySorted(QStringList list)
{
    std::sort(list.begin(), list.end(), [](const QString &a, const QString &b) { return py::less(a, b); });
    return list;
}

QStringList pySorted(const QSet<QString> &set)
{
    return pySorted(QStringList(set.cbegin(), set.cend()));
}

QString itemLine(const BlueprintItem &item)
{
    return QStringList{
        item.name,      pySorted(item.missions).join(QChar(0x1E)), item.type, item.cls, item.size, item.grade,
        item.taggedName}
        .join(QChar(0x1F));
}

} // namespace

class TestBlueprints : public QObject
{
    Q_OBJECT

private slots:
    // ── log scanner ─────────────────────────────────────────────────────

    void parseSingleEvent()
    {
        const auto evs = parseEvents(logEvent("2026-03-26T17:15:41.684Z", s("Coda Pistol")));
        QCOMPARE(evs.size(), 1);
        QCOMPARE(evs[0].name, s("Coda Pistol"));
        QCOMPARE(evs[0].timestamp, dt("2026-03-26T17:15:41.684Z"));
    }

    void parseKeepsEmbeddedQuotesAndNbsp()
    {
        const QString quoted = s("Demeco \"Purgatory Camo\" LMG");
        QCOMPARE(parseEvents(logEvent("2026-03-27T00:18:06.320Z", quoted)).at(0).name, quoted);
        const QString nbsp = s("Lynx\u00A0Legs");
        QCOMPARE(parseEvents(logEvent("2026-03-26T23:59:59.272Z", nbsp)).at(0).name, nbsp);
    }

    void parseIgnoresEchoesAndOtherMentions()
    {
        QVERIFY(parseEvents(s("<2026-03-26T17:15:47.126Z> [Notice] <UpdateNotificationItem> "
                              "Notification \"Received Blueprint: Coda Pistol: \" [23], Action: Next"))
                    .isEmpty());
        QVERIFY(parseEvents(s("<2026-03-26T17:15:41.684Z>    \"Received Blueprint: Coda Pistol: \" [23]"))
                    .isEmpty());
        QVERIFY(parseEvents(s("<2026-06-07T04:27:45.846Z> [Notice] <ReuseChannel> Reusing channel for "
                              "'sc.external.services.blueprint_library.v1.BlueprintLibraryService'"))
                    .isEmpty());
        QVERIFY(parseEvents(QString()).isEmpty());
    }

    void parseTimestamps()
    {
        QCOMPARE(parseEvents(logEvent("2026-03-26T17:15:41Z", s("F55 LMG"))).at(0).timestamp,
                 dt("2026-03-26T17:15:41Z"));
        QVERIFY(parseEvents(logEvent("2026-13-99T99:99:99Z", s("Nope"))).isEmpty());
        QCOMPARE(blueprintEpoch(), dt("2026-03-01T00:00:00Z"));
    }

    void scanFilters()
    {
        QTemporaryDir dir;
        const QString a = writeLog(dir.filePath(s("a.log")), {logEvent("2026-02-15T10:00:00Z", s("Ancient")),
                                                              logEvent("2026-03-05T10:00:00Z", s("Kept"))});
        ScanResult res = scanFiles({a});
        QCOMPARE(res.names, set({"Kept"}));
        QCOMPARE(res.eventsMatched, 1);

        // The watermark is exclusive, and the latest timestamp ignores it.
        const QString b =
            writeLog(dir.filePath(s("b.log")), {logEvent("2026-03-10T00:00:00Z", s("AtWatermark")),
                                                logEvent("2026-03-11T00:00:00Z", s("AfterWatermark"))});
        QCOMPARE(scanFiles({b}, dt("2026-03-10T00:00:00Z")).names, set({"AfterWatermark"}));
        res = scanFiles({b}, dt("2026-04-01T00:00:00Z"));
        QVERIFY(res.names.isEmpty());
        QCOMPARE(res.latestTimestamp, dt("2026-03-11T00:00:00Z"));
    }

    void scanDeduplicates()
    {
        QTemporaryDir dir;
        const QString a =
            writeLog(dir.filePath(s("a.log")), {logEvent("2026-03-05T10:00:00Z", s("Coda Pistol"))});
        const QString b =
            writeLog(dir.filePath(s("b.log")), {logEvent("2026-03-06T10:00:00Z", s("Coda Pistol"))});
        ScanResult res = scanFiles({a, b});
        QCOMPARE(res.names, set({"Coda Pistol"}));
        QCOMPARE(res.eventsMatched, 2);

        const QString c = writeLog(dir.filePath(s("c.log")),
                                   {logEvent("2026-03-09T07:54:03.720Z", s("Torrez"), 5),
                                    s("<2026-03-09T07:54:18.830Z> [Notice] <UpdateNotificationItem> "
                                      "Notification \"Received Blueprint: "
                                      "Torrez: \" [5], Action: Next")});
        res = scanFiles({c});
        QCOMPARE(res.names, set({"Torrez"}));
        QCOMPARE(res.eventsMatched, 1);
    }

    void scanLatestAndEmpty()
    {
        QTemporaryDir dir;
        const QString a = writeLog(dir.filePath(s("a.log")), {logEvent("2026-03-05T10:00:00Z", s("A")),
                                                              logEvent("2026-03-20T10:00:00Z", s("B")),
                                                              logEvent("2026-03-12T10:00:00Z", s("C"))});
        QCOMPARE(scanFiles({a}).latestTimestamp, dt("2026-03-20T10:00:00Z"));

        const QString noise = writeLog(dir.filePath(s("n.log")), {s("just noise"), s("nothing to see")});
        const ScanResult res = scanFiles({noise});
        QVERIFY(res.names.isEmpty());
        QVERIFY(!res.latestTimestamp.isValid());
        QCOMPARE(res.filesScanned, 1);
        QCOMPARE(scanFiles({}).filesScanned, 0);
    }

    void scanProgressAndMissingFiles()
    {
        QTemporaryDir dir;
        const QString a = writeLog(dir.filePath(s("a.log")), {logEvent("2026-03-05T10:00:00Z", s("A"))});
        QList<std::tuple<int, int, QString>> calls;
        const ScanResult res =
            scanFiles({dir.filePath(s("gone.log")), a}, {},
                      [&](int done, int total, const QString &name) { calls.append({done, total, name}); });
        QCOMPARE(res.names, set({"A"}));
        QCOMPARE(calls.front(), std::make_tuple(0, 2, s("gone.log")));
        QCOMPARE(calls.back(), std::make_tuple(2, 2, QString()));
    }

    void scanCrlfAndLoneCr()
    {
        QTemporaryDir dir;
        QFile f(dir.filePath(s("crlf.log")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write((logEvent("2026-03-05T10:00:00Z", s("One")) + s("\r\n") +
                 logEvent("2026-03-06T10:00:00Z", s("Two")) + s("\r") +
                 logEvent("2026-03-07T10:00:00Z", s("Three")))
                    .toUtf8());
        f.close();
        QCOMPARE(scanFiles({f.fileName()}).names, set({"One", "Two", "Three"}));
    }

    void findLogFilesOrderAndFloors()
    {
        QTemporaryDir dir;
        writeLog(dir.filePath(s("logbackups/old.log")), {s("x")}, dt("2026-03-10T00:00:00Z"));
        writeLog(dir.filePath(s("logbackups/ancient.log")), {s("x")}, dt("2026-01-01T00:00:00Z"));
        writeLog(dir.filePath(s("logbackups/notes.txt")), {s("x")}, dt("2026-03-10T00:00:00Z"));
        writeLog(dir.filePath(s("Game.log")), {s("x")}, dt("2026-03-20T00:00:00Z"));
        QCOMPARE(fileNames(findLogFiles(dir.path())), (QStringList{s("old.log"), s("Game.log")}));
        QCOMPARE(fileNames(findLogFiles(dir.path(), dt("2026-03-15T00:00:00Z"))), QStringList{s("Game.log")});
        QVERIFY(findLogFiles(dir.filePath(s("nonexistent"))).isEmpty());

        QTemporaryDir backupsOnly;
        writeLog(backupsOnly.filePath(s("logbackups/x.log")), {s("x")}, dt("2026-03-10T00:00:00Z"));
        QCOMPARE(fileNames(findLogFiles(backupsOnly.path())), QStringList{s("x.log")});
    }

    void scanChannelEndToEnd()
    {
        QTemporaryDir dir;
        writeLog(dir.filePath(s("logbackups/a.log")), {logEvent("2026-03-05T10:00:00Z", s("Coda Pistol"))},
                 dt("2026-03-05T10:00:00Z"));
        const ScanResult res = scanChannel(dir.path());
        QCOMPARE(res.names, set({"Coda Pistol"}));
        QCOMPARE(res.filesScanned, 1);
    }

    void channelsToScanPairsLiveAndHotfix()
    {
        const QStringList both = {s("LIVE"), s("HOTFIX"), s("PTU")};
        QCOMPARE(channelsToScan(s("LIVE"), true, both), (QStringList{s("LIVE"), s("HOTFIX")}));
        QCOMPARE(channelsToScan(s("HOTFIX"), true, both), (QStringList{s("HOTFIX"), s("LIVE")}));
        QCOMPARE(channelsToScan(s("LIVE"), false, both), QStringList{s("LIVE")});
        QCOMPARE(channelsToScan(s("LIVE"), true, {s("LIVE")}), QStringList{s("LIVE")});
        QCOMPARE(channelsToScan(s("PTU"), true, both), QStringList{s("PTU")});
    }

    // ── normalizeItemName ───────────────────────────────────────────────

    void normalizeBasics()
    {
        QCOMPARE(normalizeItemName(s("[Mil-S1-A] Norfield")), s("Norfield"));
        QCOMPARE(normalizeItemName(s("Antium Core <EM4>[Owned]</EM4>")), s("Antium Core"));
        QCOMPARE(normalizeItemName(s("Antium Core")), s("Antium Core"));
        QCOMPARE(normalizeItemName(s("Barbican [IND-S3-B]")), s("Barbican"));
        QCOMPARE(normalizeItemName(s("Lynx\u00A0Legs")), s("Lynx Legs"));
        QCOMPARE(normalizeItemName(s("F55  LMG   Magazine")), s("F55 LMG Magazine"));
        QCOMPARE(normalizeItemName(s("[E-S2] Omnisky VI Cannon")), s("Omnisky VI Cannon"));
        QCOMPARE(normalizeItemName(QString()), QString());
        QCOMPARE(normalizeItemName(s("Barbican [IND-S3-B]")), normalizeItemName(s("[IND-S3-B] Barbican")));
    }

    void normalizeAliases()
    {
        QCOMPARE(normalizeItemName(s("Hofstede")), s("S00 Hofstede"));
        QCOMPARE(normalizeItemName(s("Helix")), s("S0 Helix"));
        QCOMPARE(normalizeItemName(s("Klein")), s("Lawson Mining Laser"));
        for (const QString &real : bulletNameAliases())
            QCOMPARE(normalizeItemName(real), real);
        QCOMPARE(normalizeItemName(s("[Mining Laser-S0] Hofstede")), s("S00 Hofstede"));
        QCOMPARE(normalizeItemName(s("Hofstede (Mining Laser)")), s("S00 Hofstede"));
        QCOMPARE(normalizeItemName(s("Hofstede <EM4>[Owned]</EM4>")), s("S00 Hofstede"));
        QCOMPARE(normalizeItemName(s("Ferron Repeater")), s("Ferron Repeater"));
    }

    void normalizeCategoryAnnotations()
    {
        QCOMPARE(normalizeItemName(s("Bendix (Fuel Nozzle)")), s("Bendix"));
        QCOMPARE(normalizeItemName(s("Arbor MH1 Mining Laser (Mining Laser)")), s("Arbor MH1 Mining Laser"));
        QCOMPARE(normalizeItemName(s("Trawler Scraper Module (Salvage Mod)")), s("Trawler Scraper Module"));
        QCOMPARE(normalizeItemName(s("5CA 'Akura' (Shield)")), s("5CA 'Akura'"));
        QCOMPARE(normalizeItemName(s("Artimex Arms (Modified)")), s("Artimex Arms (Modified)"));
        QCOMPARE(normalizeItemName(s("Ballistic Gatling (x2)")), s("Ballistic Gatling (x2)"));
    }

    void normalizeEnclosings()
    {
        QCOMPARE(normalizeItemName(s("(Mil-S1-A) Norfield"), kRound), s("Norfield"));
        QCOMPARE(normalizeItemName(s("Barbican {IND-S3-B}"), {{s("{"), s("}")}}), s("Barbican"));
        QCOMPARE(normalizeItemName(s("<Mil-S1-A> Norfield"), {{s("<"), s(">")}}), s("Norfield"));
        QCOMPARE(normalizeItemName(s("[Mil-S1-A] Norfield"), kRound), s("[Mil-S1-A] Norfield"));
        QCOMPARE(normalizeItemName(s("Artimex Arms (Modified)"), kRound), s("Artimex Arms"));
        QCOMPARE(normalizeItemName(s("[X] Name"), Enclosings{}), s("[X] Name"));
    }

    void normalizeNoneStyle()
    {
        QCOMPARE(normalizeItemName(s("Mil-S1-A Norfield"), defaultEnclosings(), s("Norfield")),
                 s("Norfield"));
        QCOMPARE(normalizeItemName(s("Mil-S1-A Norfield"), kSquareAndNone), s("Norfield"));
        QCOMPARE(normalizeItemName(s("Norfield Mil-S1-A"), kSquareAndNone), s("Norfield"));
        // Off unless the None style is configured.
        QCOMPARE(normalizeItemName(s("F-4 Blaster")), s("F-4 Blaster"));
        QCOMPARE(normalizeItemName(s("Mil-S1-A Norfield")), s("Mil-S1-A Norfield"));
        QCOMPARE(normalizeItemName(s("[Mil-S1-A] Norfield"), kSquareAndNone), s("Norfield"));
        QCOMPARE(normalizeItemName(s("Norfield [Mil-S1-A]"), kSquareAndNone), s("Norfield"));
        QCOMPARE(normalizeItemName(s("Military S2 A Norfield")), s("Military S2 A Norfield"));
        QCOMPARE(normalizeItemName(s("10-Series Greatsword Cannon"), kSquareAndNone),
                 s("10-Series Greatsword Cannon"));
    }

    void stockDiff()
    {
        QCOMPARE(normalizeItemName(s("[Mil-S1-A] Norfield"), defaultEnclosings(), s("Norfield")),
                 s("Norfield"));
        QCOMPARE(
            normalizeItemName(s("[Mil-S1-A] Norfield"), defaultEnclosings(), s("Something Else Entirely")),
            s("Norfield"));
        QCOMPARE(stripViaStockDiff(s("Mil-S1-A Norfield"), s("Norfield")), std::optional(s("Mil-S1-A")));
        QCOMPARE(stripViaStockDiff(s("Norfield Mil-S1-A"), s("Norfield")), std::optional(s("Mil-S1-A")));
        QCOMPARE(stripViaStockDiff(s("Something Unrelated"), s("Norfield")), std::nullopt);
        QCOMPARE(stripViaStockDiff(s("Norfield"), s("Norfield")), std::optional(QString()));
        QCOMPARE(stripViaStockDiff(s("Mil-S1-A Norfield"), QString()), std::nullopt);
    }

    void enclosingsFromConfigs()
    {
        const auto configs = [](const char *c, const char *m, const char *w) {
            QMap<QString, tags::TagConfig> map;
            for (const auto &[cat, enc] : {std::pair{"components", c},
                                           {"missiles", m},
                                           {"ship_weapons", w},
                                           {"commodities", "round"},
                                           {"mission_titles", "curly"}}) {
                tags::TagConfig cfg = tags::defaultConfig(s(cat));
                cfg.enclosing = s(enc);
                map.insert(s(cat), cfg);
            }
            return map;
        };
        QCOMPARE(enclosingsFromTagConfigs(configs("square", "square", "square")), defaultEnclosings());
        const Enclosings rounds = enclosingsFromTagConfigs(configs("round", "round", "square"));
        QCOMPARE(rounds, (Enclosings{{s("("), s(")")}, {s("["), s("]")}}));
        QVERIFY(enclosingsFromTagConfigs(configs("round", "round", "round"))
                    .contains(defaultEnclosings().front()));
        QCOMPARE(enclosingsFromTagConfigs(configs("none", "angle", "square")),
                 (Enclosings{kNoneStyleEnclosing, {s("<"), s(">")}, {s("["), s("]")}}));
    }

    // ── sections and [Owned] ────────────────────────────────────────────

    void extractNames()
    {
        QCOMPARE(extractBpItemNames(kDesc), set({"Antium Core", "Norfield", "Abrade Scraper Module"}));
        QVERIFY(extractBpItemNames(s("Just a plain description, no rewards.")).isEmpty());
        QVERIFY(extractBpItemNames(QString()).isEmpty());
        QCOMPARE(extractBpItemNames(kDescWithFalsePositives),
                 set({"Cryo-Star SL", "Kelvid", "DuraJet", "ZapJet"}));
    }

    void renamedHeaders()
    {
        QString renamed = kDesc;
        renamed.replace(s("POTENTIAL BLUEPRINTS"), s("MY LOOT"));
        QVERIFY(extractBpItemNames(renamed).isEmpty());
        const QSet<QString> all = set({"Antium Core", "Norfield", "Abrade Scraper Module"});
        QCOMPARE(extractBpItemNames(renamed, defaultEnclosings(), s("MY LOOT")), all);
        QString lower = kDesc;
        lower.replace(s("POTENTIAL BLUEPRINTS"), s("my loot"));
        QCOMPARE(extractBpItemNames(lower, defaultEnclosings(), s("My Loot")), all);
        QCOMPARE(extractBpItemNames(kDesc, defaultEnclosings(), s("MY LOOT")), all);
        // Anchored to the <EM> wrapper: the word in prose does not count.
        QCOMPARE(extractBpItemNames(s("Grab that stuff for me.\\n- Ed\\n- H\\n- Wallace"
                                      "\\n\\n<EM3>stuff</EM3>\\n- Antium Core"),
                                    defaultEnclosings(), s("stuff")),
                 set({"Antium Core"}));
        QVERIFY(extractBpItemNames(s("x\\n<EM3>stuffed animals</EM3>\\n- Antium Core"), defaultEnclosings(),
                                   s("stuff"))
                    .isEmpty());
    }

    void qualifiedHeaders()
    {
        for (const char *header :
             {"Potential Blueprints (Repeat Only)", "Potential Blueprints (BitZeros Only)",
              "Potential Blueprints (Nyx Only)", "Potential Blueprints (Pyro IV/V Area Only)",
              "Multiple Blueprint Pools (Repeat Only)", "Multiple Blueprint Pools (Yormandi Eye Only)"}) {
            const QString value = s("flavor text\\n<EM4>%1</EM4>\\n- Antium Core").arg(s(header));
            QCOMPARE(extractBpItemNames(value), set({"Antium Core"}));
        }
        QVERIFY(hasBpSection(s("x\\n<EM4>POTENTIAL BLUEPRINTS</EM4>\\n- Foo")));
        QVERIFY(hasBpSection(s("x\\n<EM4>Multiple Blueprint Pools</EM4>\\n- Foo")));
        QVERIFY(!hasBpSection(s("A description with no rewards section")));
        QVERIFY(!hasBpSection(QString()));
    }

    void tiersAndPools()
    {
        QCOMPARE(extractBpItemNames(s("Adagio Holdings...\\n\\n<EM4>POTENTIAL BLUEPRINTS</EM4>"
                                      "\\n<EM4>Awarded from Contractor level variants</EM4>"
                                      "\\n- Trawler Scraper Module (Salvage Mod)"
                                      "\\n- Abrade Scraper Module (Salvage Mod)"
                                      "\\n- Cinch Scraper Module (Salvage Mod)")),
                 set({"Trawler Scraper Module", "Abrade Scraper Module", "Cinch Scraper Module"}));
        QCOMPARE(extractBpItemNames(s("\\n\\n<EM4>POTENTIAL BLUEPRINTS</EM4>"
                                      "\\n<EM4>Awarded from Jr. Contractor level variants</EM4>"
                                      "\\n- Cinch Scraper Module (Salvage Mod)"
                                      "\\n<EM4>Awarded from Contractor level variants</EM4>"
                                      "\\n- Trawler Scraper Module (Salvage Mod)"
                                      "\\n\\n<EM3>MISSION DETAILS</EM3>\\n<EM4>Difficulty:</EM4> 2")),
                 set({"Cinch Scraper Module", "Trawler Scraper Module"}));
        QCOMPARE(extractBpItemNames(s("POSTING: Purchase Order...\\n\\n<EM4>Multiple Blueprint Pools</EM4>"
                                      "\\n<EM4>Awarded from Sr. Contractor level variants</EM4>"
                                      "\\n<EM4>Pool 1</EM4>"
                                      "\\n- Arclight Pistol\\n- Venture Arms Base"
                                      "\\n\\n<EM4>Pool 2</EM4>"
                                      "\\n- Helix I Mining Laser (Mining Laser)\\n- BroadSpec (Radar)")),
                 set({"Arclight Pistol", "Venture Arms Base", "Helix I Mining Laser", "BroadSpec"}));
    }

    void applyOwned()
    {
        const QString out = applyOwnedToValue(kDesc, set({"Antium Core"}));
        QVERIFY(out.contains(s("- Antium Core <EM4>[Owned]</EM4>")));
        QVERIFY(out.contains(s("- [Mil-S1-A] Norfield\\n")));
        QVERIFY(applyOwnedToValue(kDesc, set({"Norfield"}))
                    .contains(s("- [Mil-S1-A] Norfield <EM4>[Owned]</EM4>")));
        // Idempotent, and un-owning restores the original.
        QCOMPARE(applyOwnedToValue(out, set({"Antium Core"})), out);
        QCOMPARE(out.count(s("[Owned]")), 1);
        QCOMPARE(applyOwnedToValue(out, {}), kDesc);

        const QString short_ = applyOwnedToValue(s("POSTING: mining run.\\n\\n<EM3>POTENTIAL "
                                                   "BLUEPRINTS</EM3>\\n- Hofstede\\n- Helix\\n- Norfield"),
                                                 {normalizeItemName(s("S00 Hofstede"))});
        QVERIFY(short_.contains(s("- Hofstede <EM4>[Owned]</EM4>")));
        QCOMPARE(short_.count(s("[Owned]")), 1);

        QVERIFY(!applyOwnedToValue(s("A plain line\\n- not a blueprint bullet <EM4>[Owned]</EM4>"),
                                   set({"whatever"}))
                     .contains(s("[Owned]")));
    }

    void applyOwnedStaysInSection()
    {
        const QString out =
            applyOwnedToValue(kDescWithFalsePositives, set({"Kelvid", "Council Scrip", "Stows"}));
        QVERIFY(out.contains(s("- Kelvid <EM4>[Owned]</EM4>")));
        QVERIFY(!out.contains(s("- Council Scrip <EM4>[Owned]</EM4>")));
        QVERIFY(!out.contains(s("- Stows <EM4>[Owned]</EM4>")));

        const QString value =
            s("<EM3>POTENTIAL BLUEPRINTS</EM3>\\n- Lynx\u00A0Legs\\n- Barbican [IND-S3-B]\\n- Norfield");
        const QString tagged = applyOwnedToValue(value, set({"Lynx Legs", "Barbican"}));
        QCOMPARE(tagged.count(s("<EM4>[Owned]</EM4>")), 2);
        QVERIFY(!tagged.contains(s("Norfield <EM4>[Owned]")));
        QCOMPARE(applyOwnedToValue(tagged, set({"Lynx Legs", "Barbican"})), tagged);
    }

    // ── foreign editors (#372) ──────────────────────────────────────────

    void resolveForeignNames()
    {
        const QSet<QString> reporter =
            set({"Defiant", "Colossus", "Endurance", "Huracan", "Sedulity", "Agni"});
        QCOMPARE(resolveAgainstCatalogue(s("Ind/0/B Defiant"), reporter), std::optional(s("Defiant")));
        QCOMPARE(resolveAgainstCatalogue(s("Ind/3/B Agni"), reporter), std::optional(s("Agni")));
        QCOMPARE(resolveAgainstCatalogue(s("Colossus"), reporter), std::optional(s("Colossus")));
        QCOMPARE(resolveAgainstCatalogue(s("Ind/1/B Nonesuch"), reporter), std::nullopt);
        QCOMPARE(resolveAgainstCatalogue(QString(), reporter), std::nullopt);
        QCOMPARE(resolveAgainstCatalogue(s("Civ/1/A 7SA 'Concord'"), set({"7SA 'Concord'", "Bracer"})),
                 std::optional(s("7SA 'Concord'")));
        QCOMPARE(resolveAgainstCatalogue(s("MegaColossus"), set({"Colossus"})), std::nullopt);
        QCOMPARE(resolveAgainstCatalogue(s("Ind/1/B MegaColossus"), set({"Colossus"})), std::nullopt);
        QCOMPARE(resolveAgainstCatalogue(s("Mil/1/B Fierell Cascade"), set({"Cascade", "Fierell Cascade"})),
                 std::optional(s("Fierell Cascade")));
        QCOMPARE(resolveAgainstCatalogue(s("IND.1.B.Colossus"), set({"Colossus"})),
                 std::optional(s("Colossus")));
        QCOMPARE(resolveAgainstCatalogue(s("IND:1:B:Colossus"), set({"Colossus"})),
                 std::optional(s("Colossus")));
        for (const char *decorated : {"Colossus", "[X] Colossus", "X/Colossus", "X-Colossus", "X_Colossus",
                                      "(X) Colossus", "{X} Colossus", "<X> Colossus"})
            QCOMPARE(resolveAgainstCatalogue(s(decorated), set({"Colossus"})), std::optional(s("Colossus")));
    }

    void repairOwned()
    {
        OwnedRepair r =
            repairForeignOwnedNames(set({"Cascade", "Fierell Cascade"}), set({"Cascade", "Fierell Cascade"}));
        QCOMPARE(r.repaired, set({"Cascade", "Fierell Cascade"}));
        QVERIFY(r.renamed.isEmpty());
        // A narrower catalogue reproduces the data loss the wide one avoids.
        r = repairForeignOwnedNames(set({"Cascade", "Fierell Cascade"}), set({"Cascade"}));
        QVERIFY(!r.repaired.contains(s("Fierell Cascade")));
        QCOMPARE(r.renamed.value(s("Fierell Cascade")), std::nullopt);
        QCOMPARE(r.renamed.size(), 1);

        r = repairForeignOwnedNames(set({"Cascade", "Ind/1/B Colossus"}), set({"Cascade", "Colossus"}));
        QCOMPARE(r.repaired, set({"Cascade", "Colossus"}));
        QCOMPARE(r.renamed.value(s("Ind/1/B Colossus")), std::optional(s("Colossus")));
        r = repairForeignOwnedNames(set({"Colossus", "Ind/1/B Colossus"}), set({"Colossus"}));
        QCOMPARE(r.repaired, set({"Colossus"}));
        QCOMPARE(r.renamed.value(s("Ind/1/B Colossus"), s("x")), std::nullopt);
        QVERIFY(repairForeignOwnedNames(set({"Colossus", "Ind/1/B Agni"}), {}).renamed.isEmpty());
        QVERIFY(repairForeignOwnedNames(set({"Ind/1/B Nonesuch"}), set({"Colossus"})).renamed.isEmpty());
    }

    void knownNames()
    {
        const QString ship = category::kShipItems;
        QCOMPARE(knownItemNames({entry("item_NameFierellCascade", s("Fierell Cascade"), ship)}),
                 set({"Fierell Cascade"}));
        QCOMPARE(knownItemNames({entry("vehicle_NameANVL_Hornet", s("Hornet"), ship),
                                 entry("item_mining_mininglaser_s1", s("Helix I Mining Laser"), ship)}),
                 set({"Hornet", "Helix I Mining Laser"}));
        QVERIFY(knownItemNames({entry("item_DescFierellCascade", s("A powerful shield generator."), ship),
                                entry("mission_title_001", s("Some Mission"), ship)})
                    .isEmpty());
    }

    // ── metadata ────────────────────────────────────────────────────────

    void componentTags()
    {
        const auto tag = [](const char *c, const char *sz, const char *g) {
            return ComponentTag{s(c), s(sz), s(g)};
        };
        QCOMPARE(parseComponentTag(s("[MIL-S3-B] Balandin")), tag("MIL", "S3", "B"));
        QCOMPARE(parseComponentTag(s("[ind-s1-a] Palisade")), tag("IND", "S1", "A"));
        QCOMPARE(parseComponentTag(s("Balandin")), ComponentTag{});
        QCOMPARE(parseComponentTag(QString()), ComponentTag{});
        QCOMPARE(parseComponentTag(s("[CMP.S1.B.PW] StarHeart")), tag("CMP", "S1", "B"));
        QCOMPARE(parseComponentTag(s("[MIL.S02.C] Foo")), tag("MIL", "S2", "C"));
        QCOMPARE(parseComponentTag(s("[E-S2] Gun")).size, s("S2"));
        QCOMPARE(parseComponentTag(s("Balandin [MIL-S3-B]")), tag("MIL", "S3", "B"));
        QCOMPARE(parseComponentTag(s("[MIL-S3-B] Balandin [IND-S1-A]")), tag("MIL", "S3", "B"));
        QCOMPARE(parseComponentTag(s("(MIL-S3-B) Balandin"), kRound), tag("MIL", "S3", "B"));
        QCOMPARE(parseComponentTag(s("Palisade <IND-S1-A>"), {{s("<"), s(">")}}), tag("IND", "S1", "A"));
        const Enclosings three = {{s("["), s("]")}, {s("("), s(")")}, {s("{"), s("}")}};
        QCOMPARE(parseComponentTag(s("{IND-S1-A} Palisade"), three), tag("IND", "S1", "A"));
        QCOMPARE(parseComponentTag(s("MIL-S3-B Balandin"), defaultEnclosings(), s("Balandin")),
                 tag("MIL", "S3", "B"));
        QCOMPARE(parseComponentTag(s("MIL-S3-B Balandin"), kSquareAndNone), tag("MIL", "S3", "B"));
        QCOMPARE(parseComponentTag(s("250-E Laser Pointer")), ComponentTag{});
        QCOMPARE(parseComponentTag(s("C-788 Cannon")), ComponentTag{});
        QCOMPARE(parseComponentTag(s("C-788 Canon"), defaultEnclosings(), s("C-788 Cannon")), ComponentTag{});
        QCOMPARE(parseComponentTag(s("Titan-S2 Reactor"), kSquareAndNone, s("Titan-S2 Reactor")),
                 ComponentTag{});
    }

    void keyClassifiers()
    {
        QCOMPARE(sizeFromKey(s("item_NamePOWR_ACOM_S01_StarHeart")), s("S1"));
        QCOMPARE(sizeFromKey(s("item_NameQDRV_RSI_S02_Hemera")), s("S2"));
        QCOMPARE(sizeFromKey(s("item_NameMining_Head_S00_Arbor")), s("S0"));
        QVERIFY(sizeFromKey(s("item_Name_no_size_here")).isEmpty());
        QCOMPARE(componentTypeFromKey(s("item_NameQDRV_RSI_S02_Hemera")), s("Quantum Drive"));
        QCOMPARE(componentTypeFromKey(s("item_Name_SHLD_Aspirum")), s("Shield"));
        QCOMPARE(componentTypeFromKey(s("item_NameQRDV_typo")), s("Quantum Drive"));
        QVERIFY(componentTypeFromKey(s("item_NameAEGS_Eclipse_BombRack_S03")).isEmpty());

        QCOMPARE(expandClassFullWord(s("MIL")), s("Military"));
        QCOMPARE(expandClassFullWord(s("mil")), s("Military"));
        QCOMPARE(expandClassFullWord(s("M")), s("Military"));
        QCOMPARE(expandClassFullWord(s("I")), s("Industrial"));
        QCOMPARE(expandClassFullWord(s("STH")), s("Stealth"));
        QCOMPARE(expandClassFullWord(s("CMP")), s("Competition"));
        QCOMPARE(expandClassFullWord(s("CustomLabel")), s("CustomLabel"));
        QCOMPARE(stripSizePrefix(s("S3")), s("3"));
        QCOMPARE(stripSizePrefix(s("s10")), s("10"));
        QCOMPARE(stripSizePrefix(s("3")), s("3"));
        QCOMPARE(
            cleanMissionTitle(s("Salvager Needed (Lrg. Special Order) <EM4>[BP]</EM4> <EM4>[150 REP]</EM4>")),
            s("Salvager Needed (Lrg. Special Order)"));
    }

    void typeBuckets()
    {
        const QList<std::pair<const char *, const char *>> cases = {
            {"item_NameSHLD_Aspirum", "Shield"},
            {"item_Name_rifle_behr_p4ar", "FPS Weapon"},
            {"item_Nameutfl_crossbow_ballistic_01", "FPS Weapon"},
            {"item_Name_armor_rsi_torso", "Armor"},
            {"item_Name_helmet_xyz", "Armor"},
            {"item_NameKLWE_LaserCannon_S2", "Ship Weapon"},
            {"item_NameAEGS_Bulldog_XL", "Ship Weapon"},
            {"item_NameSHLD_ACOM_S01", "Shield"},
            {"item_NameAEGS_Eclipse_BombRack", ""},
            {"item_NameMining_Head_S00_Helix_SCItem", ""},
            {"item_Name_qrt_specialist_heavy_core_01_01_01", "Armor"},
            {"item_Name_GRIN_utility_medium_core_01_01_01", "Armor"},
            {"item_NamePOWR_ACOM_S02_LuxCore_SCItem", "Power Plant"},
            {"item_NameCOOL_JUST_S02_CoolCore", "Cooler"},
            {"item_Name_gys_scoreboard_01_01_01", ""},
            {"item_Namebehr_rifle_ballistic_01_mag", "Ammo"},
            {"item_Namegrin_multitool_01_salvage_mag_empty", "Ammo"},
            {"item_NameFlair_Poster_SM_Mag_Cover", ""},
            {"item_NameMining_Head_S00_Test_mag", "Ammo"},
            {"item_Name_gys_jacket_01_01_01", "Armor"},
            {"item_Desc_987_Jacket_01_01_01", ""},
            {"", ""},
        };
        for (const auto &[key, type] : cases)
            QCOMPARE(blueprintTypeFromKey(s(key)), s(type));
    }

    void buildMetadata()
    {
        const QString missions = category::kMissions;
        const QList<StringEntry> sample = {
            entry("Adagio_Run_Levski_H_Desc_001",
                  s("Posting body.\\n\\n<EM4>POTENTIAL BLUEPRINTS</EM4>\\n- Balandin\\n- Abrade Scraper "
                    "Module"),
                  missions),
            entry("Adagio_Run_Levski_H_Title_001", s("Salvager Needed <EM4>[BP]</EM4> <EM4>[150 REP]</EM4>"),
                  missions),
            entry("item_NameQDRV_WETK_S03_Balandin", s("[MIL-S3-B] Balandin"), category::kShipItems),
        };
        const auto meta = buildBlueprintMetadata(sample);
        const BlueprintItem bal = meta.value(s("Balandin"));
        QCOMPARE(bal.type, s("Quantum Drive"));
        QCOMPARE(bal.cls, s("Military"));
        QCOMPARE(bal.size, s("3"));
        QCOMPARE(bal.grade, s("B"));
        QCOMPARE(bal.missions, set({"Salvager Needed"}));
        const BlueprintItem scraper = meta.value(s("Abrade Scraper Module"));
        QCOMPARE(scraper.type, s("Other"));
        QVERIFY(scraper.cls.isEmpty() && scraper.size.isEmpty() && scraper.grade.isEmpty());

        const auto multi = buildBlueprintMetadata({
            entry("M_One_Desc_001", s("x\\n<EM4>POTENTIAL BLUEPRINTS</EM4>\\n- Balandin"), missions),
            entry("M_One_Title_001", s("First Job"), missions),
            entry("M_Two_Desc_001", s("y\\n<EM4>POTENTIAL BLUEPRINTS</EM4>\\n- Balandin"), missions),
            entry("M_Two_Title_001", s("Second Job"), missions),
        });
        QCOMPARE(multi.value(s("Balandin")).missions, set({"First Job", "Second Job"}));

        const auto loot = QList{entry("M_Desc_001", s("x\\n<EM4>MY LOOT</EM4>\\n- Antium Core"), missions)};
        QVERIFY(
            buildBlueprintMetadata(loot, defaultEnclosings(), {}, s("MY LOOT")).contains(s("Antium Core")));
        QVERIFY(!buildBlueprintMetadata(loot).contains(s("Antium Core")));

        const auto orphan = buildBlueprintMetadata(
            {entry("Loose_Desc_001", s("x\\n<EM4>POTENTIAL BLUEPRINTS</EM4>\\n- Orphan Part"), missions)});
        QVERIFY(orphan.value(s("Orphan Part")).missions.isEmpty());

        // Only mission descriptions are scanned (#354).
        const auto commodity = buildBlueprintMetadata(
            {entry("items_commodities_iron_desc",
                   s("Iron Ore.\\n<EM3>POTENTIAL BLUEPRINTS</EM3>\\n- Power Plants: 10 items"),
                   category::kCommodities)});
        QVERIFY(!commodity.contains(s("Power Plants: 10 items")));
    }

    void buildMetadataTypes()
    {
        const QString missions = category::kMissions;
        const auto meta = buildBlueprintMetadata({
            entry(
                "M_Desc_001",
                s("x\\n<EM4>POTENTIAL BLUEPRINTS</EM4>\\n- P4-AR Rifle\\n- RSI Torso Armor\\n- Mystery Widget"
                  "\\n- S0 Helix"),
                missions),
            entry("item_Name_rifle_behr_p4ar", s("P4-AR Rifle"), category::kGear),
            entry("item_Name_armor_rsi_torso", s("RSI Torso Armor"), category::kGear),
            entry("item_NameMining_Head_S00_Helix_SCItem", s("S0 Helix"), category::kShipItems),
        });
        QCOMPARE(meta.value(s("P4-AR Rifle")).type, s("FPS Weapon"));
        QCOMPARE(meta.value(s("RSI Torso Armor")).type, s("Armor"));
        QCOMPARE(meta.value(s("Mystery Widget")).type, s("Other"));
        QCOMPARE(meta.value(s("S0 Helix")).type, s("Other"));
    }

    void manualItems()
    {
        const auto meta = buildBlueprintMetadata({});
        QSet<QString> manual;
        for (const auto &[name, type] : manualBlueprintItems()) {
            manual.insert(s(name));
            QCOMPARE(meta.value(s(name)).type, s(type));
        }
        QCOMPARE(QSet<QString>(meta.keyBegin(), meta.keyEnd()), manual);
        QCOMPARE(meta.value(s("QuadraCell")).missions, QSet{kManualMissionLabel});
        QCOMPARE(meta.value(s("FR-66")).taggedName, s("FR-66"));
        QCOMPARE(buildBlueprintMetadata({}), meta);

        const auto facets = [](const BlueprintItem &i) {
            return QStringList{i.type, i.cls, i.size, i.grade};
        };
        const QStringList powerPlant = {s("Power Plant"), s("Military"), s("1"), s("A")};
        auto loaded = buildBlueprintMetadata(
            {entry("item_NamePOWR_XNTH_S01_QuadraCell", s("[MIL-S1-A] QuadraCell"), category::kShipItems)});
        QCOMPARE(facets(loaded.value(s("QuadraCell"))), powerPlant);
        QCOMPARE(loaded.value(s("QuadraCell")).taggedName, s("[MIL-S1-A] QuadraCell"));
        loaded = buildBlueprintMetadata(
            {entry("item_NamePOWR_XNTH_S01_QuadraCell", s("(MIL-S1-A) QuadraCell"), category::kShipItems)},
            kRound);
        QCOMPARE(facets(loaded.value(s("QuadraCell"))), powerPlant);
        loaded = buildBlueprintMetadata(
            {entry("item_NamePOWR_XNTH_S01_QuadraCell", s("MIL-S1-A QuadraCell"), category::kShipItems)},
            defaultEnclosings(), {{s("item_NamePOWR_XNTH_S01_QuadraCell"), s("QuadraCell")}});
        QCOMPARE(facets(loaded.value(s("QuadraCell"))), powerPlant);
        QCOMPARE(loaded.value(s("QuadraCell")).taggedName, s("MIL-S1-A QuadraCell"));

        const auto real = buildBlueprintMetadata(
            {entry("M_Title_001", s("Real Mission"), category::kMissions),
             entry("M_Desc_001", s("x\\n<EM4>POTENTIAL BLUEPRINTS</EM4>\\n- QuadraCell"),
                   category::kMissions)});
        QCOMPARE(real.value(s("QuadraCell")).missions, set({"Real Mission"}));
    }

    void bulletFallbacks()
    {
        const QString missions = category::kMissions;
        const QString ship = category::kShipItems;
        const auto desc = [](const char *bullets) {
            return s("x\\n<EM4>POTENTIAL BLUEPRINTS</EM4>") + s(bullets);
        };
        auto meta =
            buildBlueprintMetadata({entry("M_Desc_001", desc("\\n- RN-7s"), missions),
                                    entry("item_fuelnozzle_MISC_Standard_Name", s("[FN] RN-7s"), ship)});
        QCOMPARE(meta.value(s("RN-7s")).taggedName, s("[FN] RN-7s"));
        meta = buildBlueprintMetadata(
            {entry("M_Desc_001", desc("\\n- Lancet MH1 Mining Laser"), missions),
             entry("item_Mining_MiningLaser_Greycat_1_S1", s("[ML-S1] Lancet MH1 Mining Laser"), ship)});
        QCOMPARE(meta.value(s("Lancet MH1 Mining Laser")).taggedName, s("[ML-S1] Lancet MH1 Mining Laser"));

        // CIG's de-slugified key in place of the name.
        meta = buildBlueprintMetadata(
            {entry("M_Desc_001", desc("\\n- Nozzle Fuelgiver Grin Nozzleveryfast"), missions),
             entry("Nozzle_FuelGiver_GRIN_NozzleVeryFast_Name", s("Lindstrom"), ship)});
        QVERIFY(meta.contains(s("Lindstrom")));
        QVERIFY(!meta.contains(s("Nozzle Fuelgiver Grin Nozzleveryfast")));

        meta = buildBlueprintMetadata(
            {entry("M_Desc_001", desc("\\n- Helix\\n- Klein"), missions),
             entry("item_NameMining_Head_S00_Helix_SCItem", s("[Mining Laser] S0 Helix"), ship),
             entry("item_NameMining_Head_S00_Klein_SCItem", s("Lawson Mining Laser"), ship)});
        QCOMPARE(meta.value(s("S0 Helix")).taggedName, s("[Mining Laser] S0 Helix"));
        QCOMPARE(meta.value(s("Lawson Mining Laser")).taggedName, s("Lawson Mining Laser"));
        QVERIFY(!meta.contains(s("Helix")) && !meta.contains(s("Klein")));

        // A raw blueprint filename in place of the name.
        meta = buildBlueprintMetadata(
            {entry("M_Desc_001",
                   desc("\\n- bp_craft_nozzle_fuelgiver_grin_nozzlefast (Fuel Nozzle)"
                        "\\n- bp_craft_nozzle_fuelgiver_grin_nozzleverysecure (Fuel Nozzle)"
                        "\\n- bp_craft_nozzle_fuelgiver_misc_nozzlestandard (Fuel Nozzle)"),
                   missions),
             entry("item_fuelnozzle_GRIN_Fast_Name", s("Norfield"), ship),
             entry("item_fuelnozzle_GRIN_Safe_Name", s("Harkin"), ship),
             entry("item_fuelnozzle_MISC_Standard_Name", s("RN-7s"), ship)});
        QVERIFY(meta.contains(s("Norfield")) && meta.contains(s("Harkin")) && meta.contains(s("RN-7s")));
        QVERIFY(!meta.contains(s("bp_craft_nozzle_fuelgiver_grin_nozzlefast")));

        meta = buildBlueprintMetadata({entry("M_Desc_001", desc("\\n- Totally Unknown Widget"), missions)});
        QCOMPARE(meta.value(s("Totally Unknown Widget")).type, s("Other"));
    }

    // ── export / import ─────────────────────────────────────────────────

    void exportJson()
    {
        QMap<QString, BlueprintItem> meta;
        meta.insert(s("Norfield"),
                    BlueprintItem{s("Norfield"), {}, s("Fuel Nozzle"), {}, {}, {}, s("[FN] Norfield")});
        const QString json = exportOwnedBlueprintsJson(set({"zeta", "Alpha", "beta", "Norfield"}), meta);
        const QJsonObject payload = QJsonDocument::fromJson(json.toUtf8()).object();
        QCOMPARE(payload.value(s("version")).toInt(), 1);
        QVERIFY(payload.value(s("missions")).toArray().isEmpty());
        QStringList names;
        for (const QJsonValue &b : payload.value(s("blueprints")).toArray()) {
            names << b.toObject().value(s("name")).toString();
            QCOMPARE(b.toObject().keys(), (QStringList{s("completed"), s("favorite"), s("name")}));
        }
        QCOMPARE(names, (QStringList{s("Alpha"), s("beta"), s("[FN] Norfield"), s("zeta")}));
        QVERIFY(exportOwnedBlueprintsJson({}, {}).endsWith(s("\"blueprints\": []\n}")));
        QCOMPARE(exportOwnedBlueprintsCsv(set({"Norfield", "Other"}), meta),
                 s("name,type\n[FN] Norfield,Fuel Nozzle\nOther,\n"));
    }

    void importFiles()
    {
        QTemporaryDir dir;
        const auto write = [&](const char *name, const QByteArray &bytes) {
            QFile f(dir.filePath(s(name)));
            if (!f.open(QIODevice::WriteOnly))
                qFatal("cannot write");
            f.write(bytes);
            return f.fileName();
        };
        const auto names = [](const std::expected<QSet<QString>, QString> &r) {
            return r.value_or(QSet<QString>{});
        };

        QCOMPARE(names(parseImportNames(
                     write("own.json", exportOwnedBlueprintsJson(set({"Norfield", "Harkin"}), {}).toUtf8()))),
                 set({"Norfield", "Harkin"}));
        QCOMPARE(names(parseImportNames(
                     write("scmdb.json",
                           R"({"version":1,"blueprints":[{"name":"Frost-Star SL","tag":"x","url":"y"}]})"))),
                 set({"Frost-Star SL"}));
        QCOMPARE(
            names(parseImportNames(write("tag.json", R"({"blueprints":[{"name":"[MIL-S1-A] Norfield"}]})"))),
            set({"Norfield"}));
        QCOMPARE(names(parseImportNames(
                     write("round.json", R"({"blueprints":[{"name":"(MIL-S1-A) Norfield"}]})"), kRound)),
                 set({"Norfield"}));
        QCOMPARE(names(parseImportNames(write(
                     "skip.json", R"({"blueprints":[{"name":""},{"x":1},"str",{"name":"Norfield"}]})"))),
                 set({"Norfield"}));
        QVERIFY(!parseImportNames(write("bad.json", "{not valid json")).has_value());
        QVERIFY(!parseImportNames(write("none.json", R"({"foo":"bar"})")).has_value());
        QVERIFY(!parseImportNames(write("notlist.json", R"({"blueprints":"not a list"})")).has_value());

        QCOMPARE(names(parseImportNames(
                     write("own.csv", exportOwnedBlueprintsCsv(set({"Norfield", "Harkin"}), {}).toUtf8()))),
                 set({"Norfield", "Harkin"}));
        QCOMPARE(
            names(parseImportNames(write("extra.csv", "name,type,extra\nNorfield,Fuel Nozzle,whatever\n"))),
            set({"Norfield"}));
        QCOMPARE(
            names(parseImportNames(write("bom.csv", "\xEF\xBB\xBFname,type\r\nNorfield,Fuel Nozzle\r\n"))),
            set({"Norfield"}));
        QCOMPARE(names(parseImportNames(write(
                     "quoted.csv", "type,name\r\n\"a,b\",\"Arclight \"\"Night\"\" Pistol\"\r\n\r\nx\n"))),
                 set({"Arclight \"Night\" Pistol"}));
        QCOMPARE(parseImportNames(write("noname.csv", "type\nFuel Nozzle\n")).error(),
                 s("CSV file has no \"name\" column"));
        QVERIFY(
            !parseImportNames(write("latin.csv", "name,type\r\nNorf\xFField,Fuel Nozzle\r\n")).has_value());
        QCOMPARE(parseImportNames(write("plain.txt", "Norfield")).error(), s("Unsupported file type: .txt"));
    }

    void matchImports()
    {
        ImportMatch m = matchImportNames(set({"Norfield", "Harkin", "Unknown Thing"}),
                                         set({"Norfield", "Harkin", "RN-7s"}));
        QCOMPARE(m.matched, set({"Norfield", "Harkin"}));
        QCOMPARE(m.unmatched, set({"Unknown Thing"}));
        QCOMPARE(matchImportNames(set({"Norfield"}), {}).unmatched, set({"Norfield"}));
        QVERIFY(matchImportNames({}, set({"Norfield"})).matched.isEmpty());
        m = matchImportNames(set({"Norfield"}), set({"[FN] Norfield"}));
        QCOMPARE(m.matched, set({"[FN] Norfield"}));
        m = matchImportNames(set({"Ind/1/B Colossus"}), set({"Colossus"}), set({"Colossus"}));
        QCOMPARE(m.matched, set({"Colossus"}));
        m = matchImportNames(set({"Ind/1/B Colossus"}), set({"Agni"}), set({"Colossus", "Agni"}));
        QVERIFY(m.matched.isEmpty());
    }

    // ── settings ────────────────────────────────────────────────────────

    void ownedSettings()
    {
        QTemporaryDir dir;
        Settings st(dir.filePath(s("settings.ini")));
        QVERIFY(st.ownedItems().isEmpty());
        st.setOwnedItems(set({"Antium Core"}));
        QCOMPARE(st.ownedItems(), set({"Antium Core"}));
        st.setOwnedItems(set({"Zeta", "Alpha", "Kr\u00e9"}));
        QCOMPARE(st.value(s("owned_items")).toString(), s("[\"Alpha\", \"Kr\\u00e9\", \"Zeta\"]"));
        QVERIFY(st.toggleOwnedItem(s("Norfield")));
        QVERIFY(st.ownedItems().contains(s("Norfield")));
        QVERIFY(!st.toggleOwnedItem(s("Norfield")));
        QVERIFY(!st.ownedItems().contains(s("Norfield")));
        st.setValue(s("owned_items"), s("{broken"));
        QVERIFY(st.ownedItems().isEmpty());

        QVERIFY(st.scanOtherChannels());
        st.setScanOtherChannels(false);
        QVERIFY(!st.scanOtherChannels());
        QVERIFY(!st.blueprintShowTags());
        QCOMPARE(st.missionHeader(s("blueprints")), s("POTENTIAL BLUEPRINTS"));
        st.setMissionHeader(s("blueprints"), s("MY LOOT"));
        QCOMPARE(st.missionHeader(s("blueprints")), s("MY LOOT"));
        QVERIFY(st.missionHeader(s("bogus")).isNull());
    }

    void watermarkSettings()
    {
        QTemporaryDir dir;
        Settings st(dir.filePath(s("settings.ini")));
        QVERIFY(!st.blueprintLogWatermark().isValid());
        const QDateTime when = dt("2026-03-26T17:15:41.684Z");
        st.setBlueprintLogWatermark(when);
        QCOMPARE(st.blueprintLogWatermark(), when);
        QCOMPARE(st.value(s("blueprint_log_watermark/LIVE")).toString(),
                 s("2026-03-26T17:15:41.684000+00:00"));
        st.setBlueprintLogWatermark(dt("2026-04-01T00:00:00Z"), s("HOTFIX"));
        QCOMPARE(st.value(s("blueprint_log_watermark/HOTFIX")).toString(), s("2026-04-01T00:00:00+00:00"));
        QCOMPARE(st.blueprintLogWatermark(s("HOTFIX")), dt("2026-04-01T00:00:00Z"));
        QCOMPARE(st.blueprintLogWatermark(s("LIVE")), when);
        st.setActiveChannel(s("PTU"));
        QVERIFY(!st.blueprintLogWatermark().isValid());
        // Python wrote other offsets too; garbage reads as unset.
        st.setValue(s("blueprint_log_watermark/PTU"), s("2026-04-01T02:00:00+02:00"));
        QCOMPARE(st.blueprintLogWatermark(), dt("2026-04-01T00:00:00Z"));
        st.setValue(s("blueprint_log_watermark/PTU"), s("not-a-timestamp"));
        QVERIFY(!st.blueprintLogWatermark().isValid());
    }

    // ── parity with the Python over the Kraken global.ini ───────────────

    void krakenParity()
    {
        if (!QFile::exists(krakenPath()))
            QSKIP("kraken_global_latest.ini not present");
        QFile f(QStringLiteral(SC_SOURCE_DIR "/tests/fixtures/blueprints.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject expected = QJsonDocument::fromJson(f.readAll()).object();

        const IniMap ini = loadIni(krakenPath());
        QList<StringEntry> entries;
        entries.reserve(ini.size());
        for (const auto &[key, value] : ini) {
            StringEntry e;
            e.key = key;
            e.originalValue = value;
            e.category = extractCategory(key);
            e.sourceFile = s("global");
            entries.append(e);
        }
        QCOMPARE(entries.size(), expected.value(s("entries")).toInteger());

        static const QRegularExpression foreign(
            QStringLiteral(R"(^[A-Za-z]{2,5}/\d{1,2}/[A-Za-z]%1+)").arg(py::kReSpace),
            QRegularExpression::UseUnicodePropertiesOption);
        QHash<QString, QString> stock;
        for (const auto &[key, value] : ini)
            if (key.toLower().startsWith(u"item_name") && foreign.match(value).hasMatch())
                stock.insert(key, QString(value).remove(foreign));

        struct Config
        {
            const char *name;
            Enclosings enclosings;
            QString bpHeader;
            bool useStock;
        };
        const QList<Config> configs = {
            {"square", defaultEnclosings(), {}, false},
            {"mixed", {kNoneStyleEnclosing, {s("("), s(")")}, {s("["), s("]")}}, s("MY LOOT"), false},
            {"stock", defaultEnclosings(), {}, true},
        };
        for (const Config &cfg : configs) {
            const QJsonObject want = expected.value(s("configs")).toObject().value(s(cfg.name)).toObject();
            const QHash<QString, QString> defaults = cfg.useStock ? stock : QHash<QString, QString>{};
            const auto meta = buildBlueprintMetadata(entries, cfg.enclosings, defaults, cfg.bpHeader);
            const QStringList names = pySorted(QStringList(meta.keyBegin(), meta.keyEnd()));
            QStringList items;
            for (const QString &n : names)
                items << itemLine(meta.value(n));
            QCOMPARE(items.size(), want.value(s("items_count")).toInteger());
            if (want.value(s("items")).isArray()) {
                const QJsonArray wantItems = want.value(s("items")).toArray();
                for (qsizetype i = 0; i < items.size(); ++i)
                    QCOMPARE(items[i], wantItems[i].toString());
            } else {
                QCOMPARE(digest(items), want.value(s("items")).toString());
            }

            const QSet<QString> known = knownItemNames(entries, cfg.enclosings, defaults);
            QCOMPARE(known.size(), want.value(s("known_count")).toInteger());
            QCOMPARE(digest(pySorted(known)), want.value(s("known")).toString());

            QSet<QString> owned;
            for (qsizetype i = 0; i < names.size(); i += 3)
                owned.insert(names[i]);
            QStringList applied, bullets, normalized;
            for (const StringEntry &e : entries) {
                if (e.category != category::kMissions)
                    continue;
                const QString out = applyOwnedToValue(e.originalValue, owned, cfg.enclosings, cfg.bpHeader);
                if (out != e.originalValue)
                    applied << e.key + u'=' + out;
                const QSet<QString> found = extractBpItemNames(e.originalValue, cfg.enclosings, cfg.bpHeader);
                if (!found.isEmpty())
                    bullets << e.key + u'=' + pySorted(found).join(QChar(0x1E));
            }
            for (const auto &[key, value] : ini) {
                const QString kl = key.toLower();
                if (kl.startsWith(u"item_name") || kl.startsWith(u"vehicle_name"))
                    normalized << key + u'=' + normalizeItemName(value, cfg.enclosings);
            }
            QCOMPARE(applied.size(), want.value(s("applied_count")).toInteger());
            QCOMPARE(digest(applied), want.value(s("applied")).toString());
            QCOMPARE(bullets.size(), want.value(s("bullets_count")).toInteger());
            QCOMPARE(digest(bullets), want.value(s("bullets")).toString());
            QCOMPARE(digest(normalized), want.value(s("normalized")).toString());
        }

        QSet<QString> decoratedSet;
        for (const auto &[key, value] : ini)
            if (key.startsWith(u"item_Name") && foreign.match(value).hasMatch())
                decoratedSet.insert(value);
        QSet<QString> catalogue;
        for (const QString &d : decoratedSet)
            catalogue.insert(QString(d).remove(foreign));
        QStringList resolved;
        for (const QString &d : pySorted(decoratedSet))
            resolved << d + u'=' + resolveAgainstCatalogue(d, catalogue).value_or(s("<none>"));
        QCOMPARE(resolved.size(), expected.value(s("resolved_count")).toInteger());
        QCOMPARE(digest(resolved), expected.value(s("resolved")).toString());

        const auto meta = buildBlueprintMetadata(entries);
        const QStringList names = pySorted(QStringList(meta.keyBegin(), meta.keyEnd()));
        QSet<QString> owned = set({"Not An Item", "zeta, \"quoted\""});
        for (qsizetype i = 0; i < names.size(); i += 7)
            owned.insert(names[i]);
        QCOMPARE(exportOwnedBlueprintsJson(owned, meta, dt("2026-04-02T03:04:05.678Z")),
                 expected.value(s("export_json")).toString());
        QCOMPARE(exportOwnedBlueprintsCsv(owned, meta), expected.value(s("export_csv")).toString());
    }
};

QTEST_GUILESS_MAIN(TestBlueprints)
#include "tst_blueprints.moc"
