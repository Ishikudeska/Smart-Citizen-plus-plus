#pragma once

#include "core/tags/TagBuilder.h"
#include "core/util/Progress.h"

#include <QHash>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

#include <expected>
#include <optional>

// The enhancements generator: the nine *_enhancements.ini files Smart
// Citizen layers over base.ini, built from the DataForge XML cache. Output
// matches scripts/generate_enhancements_ini.py byte for byte. Lookups run
// in parallel first, then the category passes.
namespace core {
class Settings;
}

namespace core::enh {

// Category ids, as Smart Citizen names them.
inline const QStringList kGeneratorCategories = {
    QStringLiteral("ship_descs"),
    QStringLiteral("component_descs"),
    QStringLiteral("ship_weapon_descs"),
    QStringLiteral("fps_weapon_descs"),
    QStringLiteral("mission_rewards"),
    QStringLiteral("commodity_crafting"),
    QStringLiteral("journal"),
    QStringLiteral("missile_enhancements"),
    QStringLiteral("medical_consumables"),
};

struct GeneratorOptions
{
    QString baseIni;                         // output goes beside it
    QString englishBaseIni;                  // annotation source on a non-English run; empty: baseIni
    QString forgeDir;                        // contains raw/libs/foundry/records
    std::optional<QSet<QString>> categories; // nothing: all
    QString patchesDir;                      // loc-string workarounds; empty: none

    QMap<QString, tags::TagConfig> tagConfigs; // missing categories use defaults
    bool annotateMissionDescs = true;
    QString repXpLabel = QStringLiteral("Rep");
    QHash<QString, QString> missionHeaders;
    QString missionHeaderEmTag = QStringLiteral("EM3");
    QHash<QString, bool> missionDetailFields;
    QHash<QString, bool> missionTitleTags; // "rep_track" defaults off in the app
    bool statsPrepend = false;
    bool standardizeEarnableShipNames = false;
    bool rsOreNameAnnotations = true;

    int threads = 0; // 0: one per core
};

struct GeneratorResult
{
    QStringList files; // written, absolute
    qsizetype entries = 0;
};

// The output file for a category id.
QString outputFileName(const QString &category);

// Everything but the paths, as the app's settings have it (what
// _run_enhancements_generation hands the worker).
GeneratorOptions optionsFromSettings(Settings &settings);

// Overrides from the parity runner's options JSON (keys as in
// tools/parity/run_python_generator.py); missing keys keep their value.
void applyOptionsJson(GeneratorOptions &options, const QJsonObject &json);

std::expected<GeneratorResult, QString> generateEnhancements(const GeneratorOptions &options,
                                                             ProgressSink *progress = nullptr,
                                                             const CancelToken *cancel = nullptr);

} // namespace core::enh
