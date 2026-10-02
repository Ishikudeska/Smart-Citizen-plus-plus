#pragma once

#include "core/enhancements/Context.h"

#include <QHash>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>
#include <utility>
#include <vector>

// Mission titles and descriptions: the MISSION DETAILS body (engagement,
// difficulty, spawns, reputation, turrets), haul routes in titles, the
// POTENTIAL BLUEPRINTS / ITEM REWARDS sections from contract generators,
// and the Battaglia RS tags. Ports the mission half of
// generate_enhancements_ini.py.
namespace core::enh {

// An insertion-ordered map with QString keys (a Python dict).
template <class V>
class OrderedMap
{
public:
    using Entry = std::pair<QString, V>;

    V &operator[](const QString &key)
    {
        if (const auto it = index_.constFind(key); it != index_.cend())
            return entries_[*it].second;
        index_.insert(key, entries_.size());
        entries_.push_back({key, V{}});
        return entries_.back().second;
    }
    const V *find(const QString &key) const
    {
        const auto it = index_.constFind(key);
        return it == index_.cend() ? nullptr : &entries_[*it].second;
    }
    bool contains(const QString &key) const { return index_.contains(key); }
    qsizetype size() const { return qsizetype(entries_.size()); }
    bool isEmpty() const { return entries_.empty(); }
    auto begin() { return entries_.begin(); }
    auto end() { return entries_.end(); }
    auto begin() const { return entries_.cbegin(); }
    auto end() const { return entries_.cend(); }

private:
    std::vector<Entry> entries_;
    QHash<QString, std::size_t> index_;
};

// ── engagement and routes ──────────────────────────────────────────────

QString classifyMissionEngagement(const QString &locKey); // "Ship", "FPS", "FPS & Ship"
QString routeTokenRole(const QString &var); // "from", "to" or ""
bool isRouteTitle(const QString &titleKey);
bool titleHasRouteToken(const QString &title);
QString titleRouteToken(const QString &var, const QString &bodyToken, const QString &locationDetail);

using RouteTokens = OrderedMap<QString>; // var -> "~mission(Var|Mod)"
using RouteExpandCache = QHash<QString, std::pair<RouteTokens, RouteTokens>>;
std::pair<RouteTokens, RouteTokens> expandNestedRouteVars(const QString &var, const Loc &loc, RouteExpandCache *cache);
// The route core for a haul title ("A > B"), "" when none applies.
QString deriveRouteFragment(const QList<const QString *> &bodies, const tags::TagConfig *config, const Loc &loc,
                            RouteExpandCache *cache);
// Cargo-grade words shortened at their loc keys ("Extra Small" -> "XS").
Loc sizeAbbreviationOverrides(const Loc &loc, const QSet<QString> &shortenedSizes);

// ── spawns ─────────────────────────────────────────────────────────────

enum class Spawn { Hostile, Friendly, Objective, Unknown };
// Per bucket: label -> count.
struct SpawnBreakdown
{
    QMap<QString, qint64> buckets[4];
    QMap<QString, qint64> &operator[](Spawn s) { return buckets[int(s)]; }
    const QMap<QString, qint64> &operator[](Spawn s) const { return buckets[int(s)]; }
    bool any() const;
};

std::pair<Spawn, QString> classifySpawnGroup(const QString &name, const QString &kind);
SpawnBreakdown extractSpawnCounts(Node element, const QStringList &excludeWithin = {});
QStringList formatSpawnLines(const SpawnBreakdown &breakdown);
void mergeSpawnBreakdownsMax(SpawnBreakdown &into, const SpawnBreakdown &src);
QString extractTurretInfo(Node root);

// ── mission details ────────────────────────────────────────────────────

int parseDifficultyRating(const QString &value);
QString extractDifficulty(Node element);
QString repRewardLine(const QString &fieldName, const QString &amount, const QString &repXpLabel,
                      const QString &track = {});
QStringList extractMissionFlags(Node root);
qint64 extractMissionXp(Node root, const QHash<QString, qint64> &reputation);
QString enhancementsMission(Node root, const QHash<QString, qint64> &reputation, const QString &repXpLabel,
                            const Context &ctx, const QSet<QString> *spawnAmbiguousKeys);

// ── blueprints ─────────────────────────────────────────────────────────

QString stripCigSizePrefix(const QString &name);
QString poolRankLabel(const QString &poolName);
QString nameFromBlueprintFilename(const QString &path);

struct BlueprintPools
{
    QHash<QString, QStringList> items; // pool __ref -> sorted item names
    QHash<QString, QString> names;     // pool __ref -> lower-cased filename stem
};
BlueprintPools buildBlueprintPoolLookup(const RecordStore &store, const QHash<QString, QString> &entityNames,
                                        const QHash<QString, QString> &entityNamesByFilename,
                                        const QHash<QString, QString> &entityNameTags, const QString &placement,
                                        const QHash<QString, QString> &nameFallbackTags);

// One fingerprint (item list) -> the (system, label) pairs that produced it.
using FingerprintMap = std::vector<std::pair<QStringList, std::vector<std::pair<QString, QString>>>>;
QStringList buildBlueprintBodyParts(const FingerprintMap &uniqueFps, bool allowOverrides);

// ── resource signatures (Battaglia scan/mining contracts) ──────────────

std::vector<int> rsValueSteps(const QString &ore);
QStringList formatRsDetailsLines(const QStringList &ores, const Loc &loc);
Loc mineableRsNameOverrides(const Loc &loc);
QString formatRsTag(const QStringList &ores);
// The ores a Battaglia contract targets, in ResourceType order, deduplicated.
QStringList battagliaContractOres(Node contract);

// ── the category ───────────────────────────────────────────────────────

Loc generateMissions(const Context &ctx);

} // namespace core::enh
