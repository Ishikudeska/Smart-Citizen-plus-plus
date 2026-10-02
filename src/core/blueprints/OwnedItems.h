#pragma once

#include "core/tags/TagBuilder.h"

#include <QHash>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>

// Owned-blueprint tagging (#157). The user marks blueprint items they own,
// keyed by display name, and an " <EM4>[Owned]</EM4>" tag is woven onto
// those items wherever a mission lists them under POTENTIAL BLUEPRINTS.
// The transform is idempotent (old tags are stripped first). Values use the
// in-INI literal "\n" (backslash, n) line separator. No settings access:
// callers pass the Tag Builder enclosings and the configured blueprints
// header. Ports owned_items.py.
namespace core::blueprints {

// (open, close) Tag Builder delimiters, e.g. ("[", "]").
using EnclosingPair = std::pair<QString, QString>;
using Enclosings = QList<EnclosingPair>;

// Square only: what every caller unaware of the Tag Builder style gets.
const Enclosings &defaultEnclosings();
// The "None (space only)" style. Its presence in an Enclosings list enables
// the space-only tag heuristic, which is otherwise off.
inline const EnclosingPair kNoneStyleEnclosing = {QString(), QString()};

inline const QString kBpSectionHeader = QStringLiteral("POTENTIAL BLUEPRINTS");
inline const QString kAltBpSectionHeader = QStringLiteral("MULTIPLE BLUEPRINT POOLS");

// Bullet text -> the item's real display name, for the known one-off
// mismatches (#346). Applied by normalizeItemName, so both sides fold.
const QHash<QString, QString> &bulletNameAliases();

// The tag text left after removing `stock` (the item's pre-Tag-Builder
// value) from the front or back of `tagged`: "" when they are equal,
// nothing when `stock` is empty or neither a prefix nor a suffix (#352).
std::optional<QString> stripViaStockDiff(const QString &tagged, const QString &stock);

// A leading or trailing word shaped like a "None"-style tag ("Mil-S1-A"):
// (tag word, remainder), or nothing.
std::optional<std::pair<QString, QString>> findNoneStyleTagWord(const QString &s);
QString stripNoneStyleTagHeuristic(const QString &s);

// The distinct enclosings configured for the categories that tag item
// names, always including square; sorted.
Enclosings enclosingsFromTagConfigs(const QMap<QString, tags::TagConfig> &configs,
                                    const QStringList &categories = {QStringLiteral("components"),
                                                                     QStringLiteral("missiles"),
                                                                     QStringLiteral("ship_weapons")});

// True when `value` has a blueprint section header: POTENTIAL BLUEPRINTS,
// MULTIPLE BLUEPRINT POOLS or the user's renamed header, inside an
// <EM3>/<EM4> wrapper, optionally with a "(... Only)" qualifier.
bool hasBpSection(const QString &value, const QString &bpHeader = {});

// The matching identity of an item name: NFKC-folded, [Owned] removed, Tag
// Builder tags removed (by diffing against `stock` when given, else by
// `enclosings`, else the None-style heuristic when configured), trailing
// "(Fuel Nozzle)"-style bullet annotations removed, whitespace collapsed,
// then a bulletNameAliases() lookup.
QString normalizeItemName(const QString &name, const Enclosings &enclosings = defaultEnclosings(),
                          const QString &stock = {});

// The catalogue item that a foreign-editor name ("Ind/1/B Colossus") ends
// with on a word boundary, longest first; nothing when none does (#372).
std::optional<QString> resolveAgainstCatalogue(const QString &name, const QSet<QString> &catalogue);

struct OwnedRepair
{
    QSet<QString> repaired;
    // Each changed owned name -> its recovered name, or nothing when it was
    // dropped as a duplicate of an item already owned. Empty: no change.
    QMap<QString, std::optional<QString>> renamed;
};
// `catalogue` must be every known item name, not just tracker-eligible ones.
OwnedRepair repairForeignOwnedNames(const QSet<QString> &owned, const QSet<QString> &catalogue);

// The normalized names listed in `value`'s blueprint section.
QSet<QString> extractBpItemNames(const QString &value, const Enclosings &enclosings = defaultEnclosings(),
                                 const QString &bpHeader = {});

// `value` with [Owned] on exactly the blueprint-section bullets in `owned`.
QString applyOwnedToValue(const QString &value, const QSet<QString> &owned,
                          const Enclosings &enclosings = defaultEnclosings(), const QString &bpHeader = {});

} // namespace core::blueprints
