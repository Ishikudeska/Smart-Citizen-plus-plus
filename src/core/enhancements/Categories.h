#pragma once

#include "core/enhancements/Context.h"

#include <functional>

// The per-category passes over entity records: components, missiles, ship
// and FPS weapons, ships, medical consumables. Ports scan_entity_dir,
// scan_spaceships, _mirror_scitem_siblings and the _run_gen_* functions.
namespace core::enh {

using EnhancementFn = std::function<QString(Node root)>;

struct ScanOptions
{
    std::function<QString(Node)> locKeyFn; // default: locKey
    bool generateNameTags = false;
    NameTagger nameTagger; // default: componentNameTag
    QString nameTagPlacement = QStringLiteral("prepend");
    QString separator = kEnhancementSeparator;
    bool captureAll = false;     // keep records without stats (missions)
    const Loc *tagLoc = nullptr; // English descriptions for the taggers
    bool prepend = false;
};

// {loc key: augmented value} for every record under records/<relDir>.
Loc scanEntityDir(const RecordStore &store, const QString &relDir, const EnhancementFn &fn, const Loc &loc,
                  const ScanOptions &options = {});

// Copies enhanced component values onto the other loc-key spellings the
// game may render. Returns (scitem siblings, legacy siblings).
std::pair<int, int> mirrorScitemSiblings(Loc &out, const Loc &loc);

Loc generateComponents(const Context &ctx);
Loc generateMissiles(const Context &ctx);
Loc generateShipWeapons(const Context &ctx);
Loc generateFpsWeapons(const Context &ctx);
Loc generateShips(const Context &ctx);
Loc generateMedicalConsumables(const Context &ctx);

} // namespace core::enh
