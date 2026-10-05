#pragma once

#include "core/missions/Places.h"
#include "core/text/IniFile.h"

#include <QString>
#include <QStringList>

#include <atomic>
#include <vector>

// The mission catalog: every released mission broker entry and contract with
// its text, mission giver, payout and the places it can send you, read from
// the DataForge cache. Text is in the language of the base.ini it is given;
// runtime tokens (~mission(Location)) are filled where the record decides
// them and shown as [Location] where the game picks at random.
namespace core::missions {

struct Payout
{
    enum class Kind { None, Fixed, Calculated };
    Kind kind = Kind::None;
    qint64 amount = 0; // Fixed: the reward
    qint64 max = 0;    // Fixed: the cap when bonuses raise it; 0 when none
    QString currency;  // the record's currencyType ("UEC")
    qint64 buyIn = 0;  // paid up front to take the mission
};

struct LocationSlot
{
    QString variable;       // the mission variable ("PickupLocation")
    QString token;          // the text token it fills ("Location"); may be empty
    int placeSet = -1;      // index into Catalog::placeSets
    QStringList searchTags; // the search's tag names, for a search nothing matched
};

struct Mission
{
    QString id;            // the record's or contract's name
    QString file;          // the record's path under records/
    bool contract = false; // from a contract generator, else a mission broker entry
    QString title, description, giver, category, difficulty;
    int titleVariants = 1, descriptionVariants = 1; // the game picks one at random
    QStringList systems;                            // where its places are, sorted
    Payout payout;
    std::vector<LocationSlot> locations;
};

struct Catalog
{
    std::vector<Mission> missions;
    std::vector<Place> places;
    std::vector<std::vector<int>> placeSets; // indexes into places, sorted by name
    QStringList categories, systems;         // facet values, sorted
    bool hasPlaces = false;                  // the cache has the location templates (newer caches only)
};

struct CatalogSources
{
    QString recordsDir;          // dataForgeRecordsDir()
    QString tagTable;            // dataForgeTagTablePath()
    const IniMap *loc = nullptr; // the display language's base.ini
};

Catalog buildCatalog(const CatalogSources &sources, const std::atomic<bool> *cancel = nullptr);

struct Filter
{
    enum class PayoutKind { Any, Fixed, Calculated };
    enum class Sort { Title, PayoutHigh, PayoutLow };
    QString search;   // in the title, giver, category, description or a place; any case
    QString category; // empty for any
    QString system;   // empty for any
    PayoutKind payout = PayoutKind::Any;
    Sort sort = Sort::Title;
};

// Indexes into catalog.missions that pass `filter`, in display order.
std::vector<int> filterMissions(const Catalog &catalog, const Filter &filter);

} // namespace core::missions
