#pragma once

#include "core/blueprints/OwnedItems.h"
#include "core/model/StringEntry.h"

#include <QHash>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>

#include <span>
#include <utility>

// Per-item metadata for the Blueprint Tracker's filters (#157): the
// missions an item drops from, and its type / class / size / grade. Bullet
// names are joined to the items' own item_Name entries by normalized name;
// each blueprint-bearing ..._Desc_NNN pairs with its ..._Title_NNN for the
// mission name. Empty strings stand for Python's None. Ports
// blueprint_meta.py.
namespace core::blueprints {

struct BlueprintItem
{
    QString name; // the normalized, tag-free identity
    QSet<QString> missions;
    QString type;
    QString cls;
    QString size;  // bare number ("3")
    QString grade;
    QString taggedName; // the item's own value, tag and all; `name` when unresolved

    bool operator==(const BlueprintItem &) const = default;
};

// Blueprints announced outside any mission text (#267): (name, type).
std::span<const std::pair<const char *, const char *>> manualBlueprintItems();
inline const QString kManualMissionLabel = QStringLiteral("Limited time event reward");

// The full class word for any Tag Builder length ("MIL"/"M" -> "Military");
// other tokens unchanged.
QString expandClassFullWord(const QString &cls);
QString stripSizePrefix(const QString &size); // "S3" -> "3"

// bp_craft_ / bp_rewards_ / bp_, case-insensitively: (rest, matched).
std::pair<QString, bool> stripRawBlueprintFilenamePrefix(const QString &stem);

struct ComponentTag
{
    QString cls;   // upper-cased token, e.g. "MIL"
    QString size;  // "S3"
    QString grade; // "B"
    bool operator==(const ComponentTag &) const = default;
};
// Best-effort class/size/grade from a component tag at either end of
// `value`, whatever its separator and element order.
ComponentTag parseComponentTag(const QString &value, const Enclosings &enclosings = defaultEnclosings(),
                               const QString &stock = {});

QString sizeFromKey(const QString &key);             // "..._S01_..." -> "S1"
QString componentTypeFromKey(const QString &key);    // "Quantum Drive", ...
QString blueprintTypeFromKey(const QString &key);    // component type, Ammo, FPS Weapon, Armor, Ship Weapon
QString cleanMissionTitle(const QString &value);     // first line, reward tags removed

// Every item/vehicle name in `entries`, normalized: the catalogue that
// foreign-name recovery (#372) anchors on.
QSet<QString> knownItemNames(const QList<StringEntry> &entries, const Enclosings &enclosings = defaultEnclosings(),
                             const QHash<QString, QString> &defaultValues = {});

// name -> item for every name a loaded mission lists as a blueprint reward,
// plus the manual items. `defaultValues` (key -> stock English value) lets
// tags be recovered by diffing; `bpHeader` is the configured header.
QMap<QString, BlueprintItem> buildBlueprintMetadata(const QList<StringEntry> &entries,
                                                    const Enclosings &enclosings = defaultEnclosings(),
                                                    const QHash<QString, QString> &defaultValues = {},
                                                    const QString &bpHeader = {});

} // namespace core::blueprints
