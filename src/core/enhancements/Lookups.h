#pragma once

#include "core/enhancements/RecordStore.h"
#include "core/enhancements/Stats.h"
#include "core/enhancements/Tags.h"

#include <QHash>
#include <QString>

#include <optional>

// The lookups the generator builds before its category passes. Ports
// build_ammo_lookup, build_scitem_lookups, build_controller_lookup,
// build_armor_lookup and main()'s reputation / standings builders.
namespace core::enh {

// ammoparams/<kind>: __ref (else file stem) -> record.
RecordLookup buildAmmoLookup(const RecordStore &store, const QString &relDir);

struct ScitemLookups
{
    MagazineLookup magazines;                     // entity class -> (ammo record, capacity)
    QHash<QString, QString> entityNames;          // __ref -> display name
    QHash<QString, QString> entityNamesByFilename; // lower-cased stem -> display name
    QHash<QString, QString> entityNameTags;       // __ref -> component tag for blueprint lists
};
ScitemLookups buildScitemLookups(const RecordStore &store, const Loc &loc, const tags::TagConfig *componentsConfig);

// ships/controller/controller_flight_<class>.xml, by lower-cased class.
RecordLookup buildControllerLookup(const RecordStore &store);
// ships/armor/*.xml, by lower-cased class name.
RecordLookup buildArmorLookup(const RecordStore &store);

// reward __ref -> int(float(reputationAmount)).
QHash<QString, qint64> buildReputationLookup(const RecordStore &store);

struct Standings
{
    QHash<QString, QString> ranks;  // standing __ref -> rank display name
    QHash<QString, QString> tracks; // standing __ref -> reputation track name
};
Standings buildStandings(const RecordStore &store, const Loc &englishLoc);

} // namespace core::enh
