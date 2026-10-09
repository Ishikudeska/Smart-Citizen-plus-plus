#pragma once

#include "core/enhancements/RecordStore.h"
#include "core/enhancements/Xml.h"
#include "core/text/IniFile.h"

#include <QByteArray>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

#include <array>
#include <vector>

// Where missions can send you. DataForge gives a mission's location as a tag
// search ("a ShippingHub in Stanton"), which the game resolves against the
// MissionLocationTemplate records: one per place, each tagged and named.
// Tags form a tree, and a search for a tag also finds its descendants.
//
// About half the templates carry no star system tags: the game knows where
// they are from the level layout, which DataForge does not hold. For those,
// the system, planet and moon come from the record's name
// ("ugf_security_stanton4a_..." is on Stanton 4a), else from its name
// strings' keys ("mission_location_stanton_..."); reused layouts with
// neither stay unplaced, as they are in many places at once.
namespace core::missions {

// The tag tree, from the table extractDataForge writes (raw/tags.tsv).
class TagTable
{
public:
    static TagTable load(const QString &path); // empty when missing
    static TagTable parse(const QByteArray &tsv);

    bool isEmpty() const { return tags_.isEmpty(); }
    QString name(const QString &guid) const;
    QString parent(const QString &guid) const;
    // Every tag with this name, in table order: names are not unique.
    QStringList findByName(const QString &name) const;
    // `guid` and its ancestors, nearest first.
    QStringList lineage(const QString &guid) const;
    QStringList children(const QString &guid) const;

private:
    struct Tag
    {
        QString name, parent;
    };
    QHash<QString, Tag> tags_;
    QHash<QString, QStringList> byName_;
    QHash<QString, QStringList> children_;
};

// Display text for a localization reference ("@key" or "key"): "" for a
// sentinel or a missing key. Falls back to a case-insensitive match, as the
// game does.
class LocText
{
public:
    explicit LocText(const IniMap &loc);
    QString operator()(QStringView ref) const;

private:
    const IniMap &loc_;
    QHash<QString, const QString *> lower_;
};

struct Place
{
    QString id;      // the template's __ref
    QString name;    // its "Name" string, else its address, else the record name
    QString address; // its "Address" string ("... around Daymar"); may be empty
    QString system;  // the star system's tag name ("Stanton"); may be empty
};

// A TagSearch condition matches a place when any of its terms does: all of
// the term's positive tags and none of its negative ones, among the place's
// tags of the condition's type (what it is, produces or consumes). A search
// matches when all of its conditions do.
enum class TagType { General, Produces, Consumes };
struct TagTerm
{
    QStringList positive, negative;
};
struct TagCondition
{
    TagType type = TagType::General;
    std::vector<TagTerm> terms;
};
struct LocationSearch
{
    std::vector<TagCondition> conditions;

    bool isEmpty() const { return conditions.empty(); }
    QString key() const;              // equal searches, equal keys
    QStringList positiveTags() const; // of the General conditions
};
// The TagSearch conditions of a MissionPropertyValue_Location (or of any
// value with matchConditions).
LocationSearch parseLocationSearch(enh::Node value);
// Whether `tags` (with their ancestors) pass the search's General conditions.
bool matchesGeneral(const LocationSearch &search, const QSet<QString> &tags);

class PlaceIndex
{
public:
    // Every enabled template under missiondata/pu_locations.
    static PlaceIndex build(const enh::RecordStore &store, const TagTable &tags, const LocText &loc);

    bool isEmpty() const { return places_.empty(); }
    // Sorted by name, then address.
    const std::vector<Place> &places() const { return places_; }
    // The matching places, in places() order, one per name and address.
    std::vector<int> match(const LocationSearch &search) const;
    QStringList systems() const; // sorted

private:
    static constexpr int kTagTypes = 3;
    std::vector<Place> places_;
    // Per TagType: per place, its tags and their ancestors; tag -> places
    // with it, ascending.
    std::array<std::vector<QSet<QString>>, kTagTypes> tags_;
    std::array<QHash<QString, std::vector<int>>, kTagTypes> byTag_;
};

} // namespace core::missions
