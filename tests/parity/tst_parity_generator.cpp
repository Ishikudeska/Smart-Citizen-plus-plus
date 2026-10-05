// Enhancements generator parity against the original Python (plan P4): the
// nine *_enhancements.ini files must be byte-equal to what
// scripts/generate_enhancements_ini.py writes from the same base.ini and
// DataForge cache, for the app's default options and a customised set.
//
//   SCX_SC_CACHE     folder holding base.ini, default
//                    %USERPROFILE%\Documents\Smart Citizen\LIVE\cache
//   SCX_FORGE_CACHE  DataForge cache, default
//                    %LOCALAPPDATA%\Smart Citizen\LIVE\cache\dataforge
// Needs the reference checkout ("Smart Citizen CPLusPLus/", git-ignored) and
// a Python with lxml; skips otherwise.
//
// Three courier titles take their route modifiers from Python set order, so
// they change with PYTHONHASHSEED; for those only the modifier-free text has
// to match.

#include "core/enhancements/Generator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace core;

namespace {

const QString kReference = QStringLiteral(SC_SOURCE_DIR "/Smart Citizen CPLusPLus");

// Keys whose Python value depends on the hash seed.
const QSet<QByteArray> kSeedDependentKeys = {
    "Covalex_courier_dc_small_title_001",
    "UDM_courier_dc_small_title_001",
    "redwind_courier_dc_small_title_001",
};

QByteArray withoutRouteModifiers(QByteArray line)
{
    static const QRegularExpression re(QStringLiteral(R"(~mission\((\w+)\|\w+\))"));
    return QString::fromUtf8(line).replace(re, QStringLiteral("~mission(\\1)")).toUtf8();
}

// The first difference between two INI files, empty when they match.
QString compareIni(const QString &expectedPath, const QString &actualPath)
{
    QFile a(expectedPath), b(actualPath);
    if (!a.open(QIODevice::ReadOnly))
        return QStringLiteral("python wrote no %1").arg(QFileInfo(expectedPath).fileName());
    if (!b.open(QIODevice::ReadOnly))
        return QStringLiteral("c++ wrote no %1").arg(QFileInfo(actualPath).fileName());
    const QList<QByteArray> el = a.readAll().split('\n'), al = b.readAll().split('\n');
    for (qsizetype i = 0; i < std::min(el.size(), al.size()); ++i) {
        if (el[i] == al[i])
            continue;
        const QByteArray key = el[i].left(el[i].indexOf('='));
        if (kSeedDependentKeys.contains(key) && al[i].startsWith(key + '=') &&
            withoutRouteModifiers(el[i]) == withoutRouteModifiers(al[i]))
            continue;
        return QStringLiteral("%1 line %2:\n  python: %3\n  c++:    %4")
            .arg(QFileInfo(expectedPath).fileName())
            .arg(i + 1)
            .arg(QString::fromUtf8(el[i].left(300)), QString::fromUtf8(al[i].left(300)));
    }
    if (el.size() != al.size())
        return QStringLiteral("%1: python %2 lines, c++ %3")
            .arg(QFileInfo(expectedPath).fileName())
            .arg(el.size())
            .arg(al.size());
    return {};
}

} // namespace

class TestParityGenerator : public QObject
{
    Q_OBJECT

private:
    QString python_, baseIni_, forgeDir_;

private slots:
    void initTestCase()
    {
        const QString cache = qEnvironmentVariable(
            "SCX_SC_CACHE", QDir::homePath() + QStringLiteral("/Documents/Smart Citizen/LIVE/cache"));
        forgeDir_ = qEnvironmentVariable("SCX_FORGE_CACHE",
                                         QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA")) +
                                             QStringLiteral("/Smart Citizen/LIVE/cache/dataforge"));
        baseIni_ = cache + QStringLiteral("/base.ini");
        python_ = QStandardPaths::findExecutable(QStringLiteral("python"));
        if (!QFileInfo::exists(kReference + QStringLiteral("/scripts/generate_enhancements_ini.py")))
            QSKIP("reference checkout not present");
        if (!QFileInfo::exists(baseIni_))
            QSKIP("no Smart Citizen cache (SCX_SC_CACHE)");
        if (!QFileInfo(forgeDir_ + QStringLiteral("/raw/libs/foundry/records")).isDir())
            QSKIP("no DataForge cache (SCX_FORGE_CACHE)");
        if (python_.isEmpty())
            QSKIP("python not on PATH");
        if (QProcess::execute(python_, {QStringLiteral("-c"), QStringLiteral("import lxml")}) != 0)
            QSKIP("python has no lxml");
    }

    void p4GeneratorMatchesPython_data()
    {
        QTest::addColumn<QByteArray>("options");
        QTest::newRow("defaults") << QByteArray("{}");
        // Partial TagConfig dicts: from_dict fills the rest with defaults.
        QTest::newRow("custom") << QByteArray(R"js({
            "tag_configs": {
                "components": {"elements": [{"kind": "type", "style": "long"}, {"kind": "size", "style": "size_n"},
                                            {"kind": "grade", "style": "grade_letter"}, {"kind": "class", "enabled": false}],
                               "separator": "space", "enclosing": "round", "placement": "append"},
                "missiles": {"elements": [{"kind": "size", "style": "n"}, {"kind": "ordinance", "style": "long"}],
                             "separator": "slash", "enclosing": "curly"},
                "ship_weapons": {"elements": [{"kind": "damage", "style": "med"}], "enclosing": "none"},
                "commodities": {"elements": [{"kind": "label", "style": "long"}, {"kind": "usage", "style": "long"},
                                             {"kind": "collection", "style": "long"}],
                                "separator": "dot", "usage_separator": "slash", "placement": "append"},
                "mission_titles": {"elements": [{"kind": "route"}], "placement": "prepend", "route_arrow": "shape",
                                   "title_separator": "pipe", "location_detail": "name", "rank_separator": "colon",
                                   "abbreviated_phrases": ["intro", "hauler_needed_for", "local_shipment_route",
                                                           "ling_family_rank", "cargo", "rank", "underline_direct"],
                                   "shortened_sizes": ["Small", "Medium", "Large"],
                                   "standardize_hauling_names": true}
            },
            "rep_xp_label": "XP",
            "mission_headers": {"details": "DETAILS", "items": "LOOT"},
            "mission_header_em_tag": "EM4",
            "mission_detail_fields": {"difficulty": false, "spawns": false},
            "mission_title_tags": {"rep_track": true, "ace": false},
            "stats_prepend": true,
            "standardize_earnable_ship_names": true,
            "rs_ore_name_annotations": false
        })js");
    }

    void p4GeneratorMatchesPython()
    {
        QFETCH(QByteArray, options);
        QTemporaryDir dir;
        const QString optionsPath = dir.filePath(QStringLiteral("options.json"));
        {
            QFile f(optionsPath);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(options);
        }
        const QJsonObject json = QJsonDocument::fromJson(options).object();
        QVERIFY(!json.isEmpty() || options == "{}");

        QProcess py;
        py.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        py.start(python_, {QStringLiteral(SC_SOURCE_DIR "/tools/parity/run_python_generator.py"),
                           QStringLiteral("--reference"), kReference, QStringLiteral("--base-ini"), baseIni_,
                           QStringLiteral("--forge-dir"), forgeDir_, QStringLiteral("--out"),
                           dir.filePath(QStringLiteral("python")), QStringLiteral("--options"), optionsPath});
        QVERIFY(py.waitForFinished(1800000));
        QCOMPARE(py.exitCode(), 0);

        const QString cppDir = dir.filePath(QStringLiteral("cpp"));
        QVERIFY(QDir().mkpath(cppDir));
        QVERIFY(QFile::copy(baseIni_, cppDir + QStringLiteral("/base.ini")));
        enh::GeneratorOptions opts;
        for (const QString &category : tags::kCategories)
            opts.tagConfigs.insert(category, tags::defaultConfig(category));
        opts.missionTitleTags.insert(QStringLiteral("rep_track"), false);
        enh::applyOptionsJson(opts, json);
        opts.baseIni = cppDir + QStringLiteral("/base.ini");
        opts.forgeDir = forgeDir_;
        opts.patchesDir = kReference + QStringLiteral("/patches");
        const auto result = enh::generateEnhancements(opts);
        QVERIFY2(result.has_value(), qPrintable(result ? QString() : result.error()));
        qInfo("generated %lld entries", static_cast<long long>(result->entries));

        for (const QString &category : enh::kGeneratorCategories) {
            const QString name = enh::outputFileName(category);
            const QString diff =
                compareIni(dir.filePath(QStringLiteral("python/") + name), cppDir + u'/' + name);
            QVERIFY2(diff.isEmpty(), qPrintable(diff));
        }
    }
};

QTEST_GUILESS_MAIN(TestParityGenerator)
#include "tst_parity_generator.moc"
