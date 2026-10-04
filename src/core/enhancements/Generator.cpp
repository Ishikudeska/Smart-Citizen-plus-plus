#include "core/enhancements/Generator.h"

#include "core/enhancements/Categories.h"
#include "core/enhancements/Context.h"
#include "core/enhancements/Crafting.h"
#include "core/enhancements/Missions.h"
#include "core/Settings.h"
#include "core/pipeline/Patcher.h"
#include "core/text/IniFile.h"
#include "core/text/PyText.h"

#include <QDir>
#include <QFileInfo>
#include <QFuture>
#include <QJsonArray>
#include <QLoggingCategory>
#include <QThreadPool>
#include <QtConcurrentRun>

#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(lcEnh)

namespace core::enh {

namespace {

// Earnable ships whose loc key doesn't tell them from the pledge version
// (exec-hangar PYX, Wikelo WIK). Empty entries are placeholders.
constexpr std::pair<const char *, const char *> kEarnableShipNames[] = {
    {"vehicle_NameANVL_Hornet_F7A_Mk2_PYAM_Exec", "Anvil F7A Hornet Mk II PYX"},
    {"vehicle_NameDRAK_Cutlass_Black_PYAM_Exec", "Drake Cutlass Black PYX"},
    {"vehicle_NameRSI_Meteor_Collector_Military", "RSI Meteor Collector Military PYX"},
    {"vehicle_NameANVL_Lightning_F8C_PYAM_Exec", "Anvil F8C Lightning PYX"},
    {"vehicle_NameDRAK_Corsair_PYAM_Exec", "Drake Corsair PYX"},
    {"vehicle_NameGAMA_Syulen_PYAM_Exec", "Gama Syulen PYX"},
    {"TheCollector_ShipMod_MISC_Fortune_VehicleName", "MISC Fortune WIK"},
    {"TheCollector_ShipMod_MRAI_GuardianQI_VehicleName", "Mirai Guardian QI WIK"},
    {"TheCollector_ShipMod_MRAI_Pulse_VehicleName", "Mirai Pulse WIK"},
    {"TheCollector_ShipMod_URSA_Medivac_VehicleName", "RSI Ursa Medivac WIK"},
    {"TheCollector_ShipMod_XIAN_Nox_VehicleName", "Aopoa Nox WIK"},
    {"vehicle_NameCRUS_Spirit_C1_Collector_Civilian", "Crusader C1 Spirit WIK"},
    {"vehicle_NameRSI_Polaris_Collector_Military", "RSI Polaris WIK"},
    {"vehicle_NameAEGS_Firebird_Collector_Milt", "Aegis Sabre Firebird WIK War"},
    {"vehicle_NameAEGS_Idris_P_Collector_Military", "Aegis Idris-P WIK War"},
    {"vehicle_NameANVL_Asgard_Collector_Military", ""},
    {"vehicle_NameANVL_Lightning_F8C_Collector_Military", "Anvil F8C Lightning WIK War"},
    {"vehicle_NameCRUS_Starfighter_Inferno_Collector_Military", "Crusader Ares Star Fighter Inferno WIK War"},
    {"vehicle_NameCRUS_Starlifter_A2_Collector_Military", "Crusader A2 Hercules Starlifter WIK War"},
    {"vehicle_NameKRIG_L21_Wolf_Collector_Military", "Kruger L-21 Wolf WIK War"},
    {"vehicle_NameKRIG_L22_Alpha_Wolf_Collector_Military", "Kruger L-22 Alpha Wolf WIK War"},
    {"vehicle_NameMISC_Starlancer_TAC_Collector_Military", "MISC Starlancer TAC WIK War"},
    {"vehicle_NameMRAI_Guardian_Collector_Military", "Mirai Guardian WIK War"},
    {"vehicle_NameMRAI_Guardian_MX_Collector_Military", "Mirai Guardian MX WIK War"},
    {"vehicle_NameRSI_Constellation_Taurus_Collector_Military", "RSI Constellation Taurus WIK War"},
    {"vehicle_NameANVL_Lightning_F8C_Collector_Stealth", "Anvil F8C Lightning WIK Stealth"},
    {"vehicle_NameCRUS_Starfighter_Ion_Collector_Stealth", "Crusader Ares Star Fighter Ion WIK Stealth"},
    {"vehicle_NameKRIG_L21_Wolf_Collector_Stealth", "Kruger L-21 Wolf WIK Stealth"},
    {"vehicle_NameRSI_Apollo_Triage_Collector_Stealth", "RSI Apollo Triage WIK Stealth"},
    {"vehicle_NameRSI_Meteor_Collector_Stealth", "RSI Meteor WIK Stealth"},
    {"vehicle_NameRSI_Scorpius_Collector_Stealth", "RSI Scorpius WIK Stealth"},
    {"vehicle_NameARGO_RAFT_Collector_Indust", "Argo RAFT WIK Work"},
    {"vehicle_NameCRUS_Intrepid_Collector_Indust", "Crusader Intrepid WIK Work"},
    {"vehicle_NameDRAK_Golem_Collector_Indust", "Drake Golem WIK Work"},
    {"vehicle_NameESPR_Prowler_Utility_Collector_Indust", "Prowler Utility WIK Work"},
    {"vehicle_NameMISC_Prospector_Collector_Indust", "MISC Prospector WIK Work"},
    {"vehicle_NameMISC_Starlancer_MAX_Collector_Indust", "MISC Starlancer MAX WIK Work"},
    {"vehicle_NameRSI_Zeus_CL_Collector_Indust", "RSI Zeus Mk II CL WIK Work"},
    {"vehicle_NameRSI_Zeus_ES_Collector_Indust", "RSI Zeus Mk II ES WIK Work"},
};

// write_ini: sorted by key, '\n' written as CRLF, UTF-8 without BOM.
bool writeSorted(const QString &path, const Loc &entries)
{
    QList<Loc::Entry> sorted = entries.entries();
    std::sort(sorted.begin(), sorted.end(), [](const Loc::Entry &a, const Loc::Entry &b) {
        return a.first != b.first ? py::less(a.first, b.first) : py::less(a.second, b.second);
    });
    Loc ordered;
    ordered.reserve(sorted.size());
    for (const auto &[k, v] : sorted)
        ordered.insert(k, v);
    return writeIniFile(path, ordered);
}

} // namespace

QString outputFileName(const QString &category)
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("ship_descs"), QStringLiteral("ships_desc_enhancements.ini")},
        {QStringLiteral("component_descs"), QStringLiteral("components_desc_enhancements.ini")},
        {QStringLiteral("ship_weapon_descs"), QStringLiteral("ship_weapons_desc_enhancements.ini")},
        {QStringLiteral("fps_weapon_descs"), QStringLiteral("fps_weapons_desc_enhancements.ini")},
        {QStringLiteral("mission_rewards"), QStringLiteral("mission_rewards_enhancements.ini")},
        {QStringLiteral("commodity_crafting"), QStringLiteral("commodity_crafting_enhancements.ini")},
        {QStringLiteral("journal"), QStringLiteral("journal_enhancements.ini")},
        {QStringLiteral("missile_enhancements"), QStringLiteral("missile_enhancements.ini")},
        {QStringLiteral("medical_consumables"), QStringLiteral("medical_consumables_enhancements.ini")},
    };
    return names.value(category);
}

GeneratorOptions optionsFromSettings(Settings &settings)
{
    GeneratorOptions options;
    QSet<QString> categories;
    for (const auto ids = settings.enabledEnhancementFileIds(); const QString &id : ids)
        categories.insert(id);
    options.categories = categories;
    options.tagConfigs = settings.allTagConfigs();
    options.annotateMissionDescs = settings.annotateMissionDescs();
    options.repXpLabel = settings.repXpLabel();
    for (const char *key : {"details", "blueprints", "items", "blueprint_data"})
        options.missionHeaders.insert(QString::fromLatin1(key), settings.missionHeader(QString::fromLatin1(key)));
    options.missionHeaderEmTag = settings.missionHeaderEmTag();
    for (const QString &field : kMissionFieldKeys)
        options.missionDetailFields.insert(field, settings.missionDetailField(field));
    for (const QString &field : kMissionTitleTagKeys)
        options.missionTitleTags.insert(field, settings.missionTitleTag(field));
    options.statsPrepend = settings.statsPrepend();
    options.standardizeEarnableShipNames = settings.standardizeEarnableShipNames();
    options.rsOreNameAnnotations = settings.rsOreNameAnnotations();
    return options;
}

void applyOptionsJson(GeneratorOptions &options, const QJsonObject &json)
{
    const auto boolMap = [](const QJsonValue &v, QHash<QString, bool> &into) {
        const QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it)
            into.insert(it.key(), it.value().toBool());
    };
    if (const QJsonArray cats = json.value(u"categories").toArray(); !cats.isEmpty()) {
        QSet<QString> set;
        for (const QJsonValue &c : cats)
            set.insert(c.toString());
        options.categories = set;
    }
    const QJsonObject configs = json.value(u"tag_configs").toObject();
    for (auto it = configs.begin(); it != configs.end(); ++it)
        options.tagConfigs.insert(it.key(), tags::TagConfig::fromObject(it.value().toObject()));
    const QJsonObject headers = json.value(u"mission_headers").toObject();
    for (auto it = headers.begin(); it != headers.end(); ++it)
        options.missionHeaders.insert(it.key(), it.value().toString());
    boolMap(json.value(u"mission_detail_fields"), options.missionDetailFields);
    boolMap(json.value(u"mission_title_tags"), options.missionTitleTags);
    options.englishBaseIni = json.value(u"english_base_ini").toString(options.englishBaseIni);
    options.annotateMissionDescs = json.value(u"annotate_mission_descs").toBool(options.annotateMissionDescs);
    options.repXpLabel = json.value(u"rep_xp_label").toString(options.repXpLabel);
    options.missionHeaderEmTag = json.value(u"mission_header_em_tag").toString(options.missionHeaderEmTag);
    options.statsPrepend = json.value(u"stats_prepend").toBool(options.statsPrepend);
    options.standardizeEarnableShipNames =
        json.value(u"standardize_earnable_ship_names").toBool(options.standardizeEarnableShipNames);
    options.rsOreNameAnnotations = json.value(u"rs_ore_name_annotations").toBool(options.rsOreNameAnnotations);
}

std::expected<GeneratorResult, QString> generateEnhancements(const GeneratorOptions &options, ProgressSink *progress,
                                                             const CancelToken *cancel)
{
    const auto want = [&](const char *category) {
        return !options.categories || options.categories->contains(QString::fromLatin1(category));
    };
    const auto cancelled = [&] { return cancel && cancel->isCancelled(); };
    const auto tick = [&](const QString &message) {
        qCInfo(lcEnh).noquote() << "CHECKPOINT:" << message;
        if (progress)
            progress->advance(1, message);
    };

    const QFileInfo baseInfo(options.baseIni);
    if (!baseInfo.exists())
        return std::unexpected(QStringLiteral("base.ini not found at %1").arg(options.baseIni));
    const QString outputDir = baseInfo.absolutePath();
    const Loc loc = loadIni(options.baseIni);
    Loc englishStorage;
    const Loc *english = &loc;
    if (!options.englishBaseIni.isEmpty() && QFileInfo(options.englishBaseIni) != baseInfo &&
        QFileInfo::exists(options.englishBaseIni)) {
        englishStorage = loadIni(options.englishBaseIni);
        english = &englishStorage;
    }

    const QString records = QDir(options.forgeDir).filePath(QStringLiteral("raw/libs/foundry/records"));
    if (!QFileInfo(options.forgeDir).isDir() || !QFileInfo(records).isDir())
        return std::unexpected(QStringLiteral("DataForge cache not found at %1. Extract DataForge first.").arg(options.forgeDir));
    const std::shared_ptr<const RecordStore> store = RecordStore::scan(records);
    qCInfo(lcEnh) << "XML index:" << store->fileCount() << "files";

    const bool needAmmo = want("ship_weapon_descs") || want("fps_weapon_descs");
    const bool needNames = want("mission_rewards") || want("commodity_crafting") || want("journal");
    const int phases = 1 + (needAmmo ? 1 : 0) + (needAmmo || needNames ? 1 : 0) + (want("component_descs") ? 1 : 0) +
                       (want("missile_enhancements") ? 1 : 0) + (want("ship_weapon_descs") ? 1 : 0) +
                       (want("fps_weapon_descs") ? 1 : 0) + (want("ship_descs") ? 2 : 0) +
                       (want("mission_rewards") ? 4 : 0) + (want("commodity_crafting") || want("journal") ? 1 : 0) +
                       (want("medical_consumables") ? 1 : 0) + 1;
    if (progress)
        progress->setTotal(phases);
    tick(QStringLiteral("Loaded base.ini (%1 keys)").arg(loc.size()));

    QThreadPool pool;
    pool.setMaxThreadCount(options.threads > 0 ? options.threads : std::max(2, QThread::idealThreadCount()));

    Context ctx;
    ctx.store = store;
    ctx.loc = &loc;
    ctx.tagLoc = english;
    ctx.tagConfigs = options.tagConfigs;
    ctx.annotateMissionDescs = options.annotateMissionDescs;
    ctx.repXpLabel = options.repXpLabel.isEmpty() ? QStringLiteral("Rep") : options.repXpLabel;
    ctx.missionHeaders = options.missionHeaders;
    ctx.missionHeaderEm = options.missionHeaderEmTag.isEmpty() ? QStringLiteral("EM3") : options.missionHeaderEmTag;
    ctx.missionDetailFields = options.missionDetailFields;
    ctx.missionTitleTags = options.missionTitleTags;
    ctx.statsPrepend = options.statsPrepend;
    ctx.rsOreNameAnnotations = options.rsOreNameAnnotations;

    // Wave 1: independent lookups.
    {
        QList<QFuture<void>> jobs;
        if (needAmmo) {
            jobs << QtConcurrent::run(&pool, [&] { ctx.vehicleAmmo = buildAmmoLookup(*store, QStringLiteral("ammoparams/vehicle")); });
            jobs << QtConcurrent::run(&pool, [&] { ctx.fpsAmmo = buildAmmoLookup(*store, QStringLiteral("ammoparams/fps")); });
        }
        if (needAmmo || needNames)
            jobs << QtConcurrent::run(&pool, [&] {
                ctx.scitem = buildScitemLookups(*store, *english, &ctx.config(QStringLiteral("components")));
            });
        if (want("ship_descs")) {
            jobs << QtConcurrent::run(&pool, [&] { ctx.controllers = buildControllerLookup(*store); });
            jobs << QtConcurrent::run(&pool, [&] { ctx.armor = buildArmorLookup(*store); });
        }
        if (want("mission_rewards")) {
            jobs << QtConcurrent::run(&pool, [&] { ctx.reputation = buildReputationLookup(*store); });
            jobs << QtConcurrent::run(&pool, [&] { ctx.standings = buildStandings(*store, *english); });
        }
        for (QFuture<void> &job : jobs)
            job.waitForFinished();
        if (needAmmo)
            tick(QStringLiteral("Built ammo lookups"));
        if (needAmmo || needNames)
            tick(QStringLiteral("Built scitem lookups"));
        if (want("ship_descs"))
            tick(QStringLiteral("Built ship controller + armor lookups"));
        if (want("mission_rewards")) {
            tick(QStringLiteral("Built reputation lookup"));
            tick(QStringLiteral("Built standings + track lookups"));
        }
    }
    if (cancelled())
        return std::unexpected(QStringLiteral("Cancelled"));

    // Wave 2: the categories.
    Loc outComponents, outMissiles, outShipWeapons, outFpsWeapons, outShips, outMissions, outCommodities, outJournal,
        outMedical;
    {
        struct Job
        {
            const char *name;
            QFuture<void> future;
        };
        QList<Job> jobs;
        const auto run = [&](const char *name, auto fn) { jobs << Job{name, QtConcurrent::run(&pool, fn)}; };
        if (want("component_descs"))
            run("components", [&] { outComponents = generateComponents(ctx); });
        if (want("missile_enhancements"))
            run("missiles", [&] { outMissiles = generateMissiles(ctx); });
        if (want("ship_weapon_descs"))
            run("ship_weapons", [&] { outShipWeapons = generateShipWeapons(ctx); });
        if (want("fps_weapon_descs"))
            run("fps_weapons", [&] { outFpsWeapons = generateFpsWeapons(ctx); });
        if (want("ship_descs"))
            run("ships", [&] { outShips = generateShips(ctx); });
        if (want("mission_rewards"))
            run("missions", [&] { outMissions = generateMissions(ctx); });
        if (want("commodity_crafting") || want("journal"))
            run("commodity_journal", [&] { std::tie(outCommodities, outJournal) = generateCommodityJournal(ctx); });
        if (want("medical_consumables"))
            run("medical_consumables", [&] { outMedical = generateMedicalConsumables(ctx); });
        for (Job &job : jobs) {
            job.future.waitForFinished();
            tick(QStringLiteral("Finished %1").arg(QString::fromLatin1(job.name)));
        }
    }
    if (cancelled())
        return std::unexpected(QStringLiteral("Cancelled"));

    // Workarounds for CIG loc-pointer bugs the game reads straight from the p4k.
    if (!options.patchesDir.isEmpty()) {
        const QList<LocstringWorkaround> workarounds = loadLocstringWorkarounds(options.patchesDir);
        if (!workarounds.isEmpty()) {
            int applied = 0;
            for (Loc *out : {&outMissions, &outComponents, &outShipWeapons, &outFpsWeapons, &outShips, &outMissiles,
                             &outCommodities, &outJournal, &outMedical})
                applied += applyLocstringWorkarounds(*out, workarounds);
            qCInfo(lcEnh) << "Loc-string workarounds:" << applied << "/" << workarounds.size() << "applied";
        }
    }

    if (want("ship_descs") && options.standardizeEarnableShipNames)
        for (const auto &[key, name] : kEarnableShipNames)
            if (*name)
                outShips.insert(QString::fromLatin1(key), QString::fromUtf8(name));

    GeneratorResult result;
    const std::pair<const char *, const Loc *> files[] = {
        {"ship_descs", &outShips},           {"component_descs", &outComponents},
        {"ship_weapon_descs", &outShipWeapons}, {"fps_weapon_descs", &outFpsWeapons},
        {"mission_rewards", &outMissions},   {"commodity_crafting", &outCommodities},
        {"journal", &outJournal},            {"missile_enhancements", &outMissiles},
        {"medical_consumables", &outMedical},
    };
    for (const auto &[category, entries] : files) {
        if (!want(category))
            continue;
        const QString path = QDir(outputDir).filePath(outputFileName(QString::fromLatin1(category)));
        if (!writeSorted(path, *entries))
            return std::unexpected(QStringLiteral("Could not write %1").arg(path));
        result.files << path;
        result.entries += entries->size();
    }
    tick(QStringLiteral("Wrote all output files"));
    if (progress)
        progress->flush();
    return result;
}

} // namespace core::enh
