#pragma once

#include <QString>
#include <QStringView>

namespace core {

// Category names shown in the strings table. kMissions is also a gate: the
// blueprint scan only reads entries carrying exactly this category, so it is
// an internal token and never translated.
namespace category {
inline const QString kShips = QStringLiteral("Ships");
inline const QString kShipItems = QStringLiteral("Ship Items");
inline const QString kGear = QStringLiteral("Gear");
inline const QString kMissions = QStringLiteral("Missions");
inline const QString kCommodities = QStringLiteral("Commodities");
inline const QString kJournal = QStringLiteral("Journal");
inline const QString kMedicalConsumables = QStringLiteral("Medical Consumables");
inline const QString kOther = QStringLiteral("Other");
} // namespace category

enum class EntryStatus {
    Unmodified, // stock base.ini text
    Modified,   // the user set a custom value
    Enhanced,   // produced by the enhancements generator
    New,        // not in the stock base.ini at all
};

QString statusName(EntryStatus status); // "Unmodified", "Modified", ...

// Smart Citizen's StringEntry.extract_category: the table category for a
// loc key, from its prefix and shape. Memoized; thread-safe.
QString extractCategory(const QString &key);

// Key-shape tests the category rules share with the blueprint classifier:
// FPS weapon tokens ("_rifle_", ...), armour/gear tokens ("helmet", ...) in
// a lower-cased key, and a ship-weapon size designator (_S2, _XL, _L-2).
bool hasFpsWeaponWord(QStringView lowerKey);
bool hasArmorGearWord(QStringView lowerKey);
bool hasShipWeaponSize(const QString &key);

// A ship/vehicle NAME key (as opposed to a description in the same
// category). Only names take the favourite prefix and ASOP sort order (#329).
bool isShipNameKey(QStringView key);

struct StringEntry
{
    QString key;
    QString sourceFile; // the base source that supplied the value ("global", "enhancements", "user")
    QString category;
    QString originalValue; // merged baseline from the sources, before user edits
    QString customValue;   // the user's edit, or empty
    EntryStatus status = EntryStatus::Unmodified;

    bool isModified() const { return !customValue.isEmpty() && customValue != originalValue; }

    // Favourite prefix and sort order apply only to Ships-category name rows.
    // The stored category is the authority: enhancement keys get their
    // category from the file they came from, not the key shape.
    bool isFavoritableShip() const { return category == category::kShips && isShipNameKey(key); }
};

} // namespace core
