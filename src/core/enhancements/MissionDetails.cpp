#include "core/enhancements/Missions.h"

#include "core/enhancements/Stats.h"
#include "core/text/PyFormat.h"
#include "core/text/PyText.h"

#include <array>

namespace core::enh {

namespace {

const QRegularExpression &routeTokenRe()
{
    static const QRegularExpression re =
        py::re(QStringLiteral(R"(~mission\(\s*([A-Za-z][A-Za-z0-9_]*)\s*(\|[^)]*)?\))"));
    return re;
}

} // namespace

QString routeTokenRole(const QString &var)
{
    const QString v = var.toLower();
    if (v.startsWith(u"location") || v.startsWith(u"pickup"))
        return QStringLiteral("from");
    if (v.startsWith(u"destination") || v.startsWith(u"dropoff"))
        return QStringLiteral("to");
    return QString();
}

namespace {

QString tokenText(const QRegularExpressionMatch &m)
{
    return QStringLiteral("~mission(%1%2)").arg(m.captured(1), m.captured(2));
}

RouteTokens agreedEndpointTokens(const std::vector<RouteTokens> &perBody)
{
    RouteTokens out;
    if (perBody.empty())
        return out;
    for (const auto &[var, token] : perBody.front()) {
        bool everywhere = true;
        for (std::size_t i = 1; i < perBody.size() && everywhere; ++i)
            everywhere = perBody[i].contains(var);
        if (everywhere)
            out[var] = token;
    }
    return out;
}

// Python int() of a count attribute: anything else raises.
qint64 requireInt(std::string_view text)
{
    const std::optional<qint64> v = py::toInt(qs(text));
    if (!v)
        throw PyError("invalid literal for int()");
    return *v;
}

struct SpawnKeyword
{
    const char *substring;
    const char *kind; // "ship", "npc" or nullptr for either
    Spawn bucket;
    const char *label;
};

// Earlier entries win: specific names, factions, civilians, objectives,
// allies (hostile in mercenary context), NPC roles, generic hostile words,
// wave/tier rollups, the generic friendly catch-all.
constexpr SpawnKeyword kSpawnKeywords[] = {
    {"shiptodefend", "ship", Spawn::Friendly, "Ships to Defend"},
    {"ship to defend", "ship", Spawn::Friendly, "Ships to Defend"},
    {"escortship", "ship", Spawn::Friendly, "Escort Wings"},
    {"escort ship", "ship", Spawn::Friendly, "Escort Wings"},
    {"salvageable", "ship", Spawn::Friendly, "Salvageable Ships"},
    {"recipientship", "ship", Spawn::Friendly, "Recipient"},
    {"recipient", "ship", Spawn::Friendly, "Recipient"},
    {"friendlyship", "ship", Spawn::Friendly, "Friendlies"},
    {"interdiction", "ship", Spawn::Hostile, "Interdiction Ships"},
    {"acepilotship", "ship", Spawn::Hostile, "Ace Pilots"},
    {"acepilot", "ship", Spawn::Hostile, "Ace Pilots"},
    {"ace pilot", "ship", Spawn::Hostile, "Ace Pilots"},
    {"heist", "ship", Spawn::Hostile, "Heist Target"},
    {"security ships", "ship", Spawn::Hostile, "Security Forces"},
    {"security_ships", "ship", Spawn::Hostile, "Security Forces"},
    {"initialenemies", "ship", Spawn::Hostile, "Hostile Wave"},
    {"pirate", nullptr, Spawn::Hostile, "Pirates"},
    {"bandit", nullptr, Spawn::Hostile, "Bandits"},
    {"xeno", "ship", Spawn::Hostile, "Xeno Threat"},
    {"vulture", "ship", Spawn::Hostile, "Xeno Threat"},
    {"ninetails", nullptr, Spawn::Hostile, "Nine Tails"},
    {"nine tails", nullptr, Spawn::Hostile, "Nine Tails"},
    {"nine_tails", nullptr, Spawn::Hostile, "Nine Tails"},
    {"mauler", "ship", Spawn::Hostile, "Maulers"},
    {"polaris", "ship", Spawn::Hostile, "Polaris"},
    {"prospector", "ship", Spawn::Hostile, "Prospectors"},
    {"kopion", nullptr, Spawn::Hostile, "Kopions"},
    {"private security", "npc", Spawn::Hostile, "Private Security"},
    {"privatesecurity", "npc", Spawn::Hostile, "Private Security"},
    {"privsec", "npc", Spawn::Hostile, "Private Security"},
    {"civilian", nullptr, Spawn::Friendly, "Civilians"},
    {"civillian", nullptr, Spawn::Friendly, "Civilians"}, // CIG typo
    {"civs", "npc", Spawn::Friendly, "Civilians"},
    {"civ", "npc", Spawn::Friendly, "Civilians"},
    {"hostage", "npc", Spawn::Friendly, "Hostages"},
    {"probe ", "ship", Spawn::Objective, "Probe"},
    {"probe1", "ship", Spawn::Objective, "Probe"},
    {"probe2", "ship", Spawn::Objective, "Probe"},
    {"probe3", "ship", Spawn::Objective, "Probe"},
    {"allies", "ship", Spawn::Hostile, "Hostile Allies"},
    {"ally", "ship", Spawn::Hostile, "Hostile Allies"},
    {"boss", "npc", Spawn::Hostile, "Boss"},
    {"backup", "npc", Spawn::Hostile, "Backup"},
    {"juggernaut", nullptr, Spawn::Hostile, "Juggernauts"},
    {"sniper", "npc", Spawn::Hostile, "Snipers"},
    {"cqc", "npc", Spawn::Hostile, "CQC"},
    {"soldier", "npc", Spawn::Hostile, "Soldiers"},
    {"techie", "npc", Spawn::Hostile, "Technicians"},
    {"techi", "npc", Spawn::Hostile, "Technicians"},
    {"tech", "npc", Spawn::Hostile, "Technicians"},
    {"captain", "npc", Spawn::Hostile, "Captain"},
    {"sentry", "npc", Spawn::Hostile, "Sentries"},
    {"guard", "npc", Spawn::Hostile, "Guards"},
    {"grunt", "npc", Spawn::Hostile, "Grunts"},
    {"attacker", "npc", Spawn::Hostile, "Attackers"},
    {"target", nullptr, Spawn::Hostile, "Targets"},
    {"reinforcement", nullptr, Spawn::Hostile, "Reinforcements"},
    {"reinforcments", nullptr, Spawn::Hostile, "Reinforcements"}, // CIG typo
    {"enemy", nullptr, Spawn::Hostile, "Hostiles"},
    {"enemies", nullptr, Spawn::Hostile, "Hostiles"},
    {"hostile", nullptr, Spawn::Hostile, "Hostiles"},
    {"defender", nullptr, Spawn::Hostile, "Defenders"},
    {"wave", "ship", Spawn::Hostile, "Hostile Wave"},
    {"level ", "npc", Spawn::Hostile, "Hostiles"},
    {"lightspawn", "npc", Spawn::Hostile, "Hostiles"},
    {"mediumspawn", "npc", Spawn::Hostile, "Hostiles"},
    {"heavyspawn", "npc", Spawn::Hostile, "Hostiles"},
    {"basic", "npc", Spawn::Hostile, "Hostiles"},
    {"easy", "npc", Spawn::Hostile, "Hostiles"},
    {"medium", "npc", Spawn::Hostile, "Hostiles"},
    {"hard", "npc", Spawn::Hostile, "Hostiles"},
    {"exterior", "npc", Spawn::Hostile, "Hostiles"},
    {"defence", "npc", Spawn::Hostile, "Hostiles"},
    {"building", "npc", Spawn::Hostile, "Hostiles"},
    {"spawncloset", "npc", Spawn::Hostile, "Hostiles"},
    {"escort", nullptr, Spawn::Friendly, "Escorts"},
    {"friendly", nullptr, Spawn::Friendly, "Friendlies"},
    {"protect", nullptr, Spawn::Friendly, "Protected"},
};

// A player-relative MissionProperty wrapper rescues an unclassified group.
std::optional<std::pair<Spawn, QString>> wrapperHostileFallback(Node group)
{
    static const std::array<std::pair<const char *, const char *>, 5> wrappers = {{
        {"hostileshipspawn", "Hostiles"},
        {"hostilespawn", "Hostiles"},
        {"targetspawn", "Targets"},
        {"eliminate", "Hostiles"},
        {"boss", "Boss"},
    }};
    for (Node node = group.parent(); node && node.type() == pugi::node_element; node = node.parent()) {
        if (tag(node) != "MissionProperty")
            continue;
        const QString name = qs(getOr(node, "missionVariableName")).toLower();
        for (const auto &[substring, label] : wrappers)
            if (name.contains(QLatin1StringView(substring)))
                return std::pair{Spawn::Hostile, QString::fromLatin1(label)};
        return std::nullopt;
    }
    return std::nullopt;
}

bool withinExcludedSubtree(Node node, Node scope, const QStringList &excluded)
{
    if (excluded.isEmpty())
        return false;
    for (Node parent = node.parent(); parent && parent != scope; parent = parent.parent())
        if (parent.type() == pugi::node_element && excluded.contains(qs(tag(parent))))
            return true;
    return false;
}

void addSpawn(SpawnBreakdown &b, Spawn bucket, const QString &label, qint64 count)
{
    if (count > 0)
        b[bucket][label] += count;
}

} // namespace

QString classifyMissionEngagement(const QString &locKey)
{
    if (locKey.isEmpty())
        return QStringLiteral("Ship");
    const QString key = locKey.toLower();
    static const QStringList fps = {QStringLiteral("_fps_"),   QStringLiteral("fps_"),     QStringLiteral("_fps"),
                                    QStringLiteral("fpsmine"), QStringLiteral("_ugf_"),    QStringLiteral("ugf_"),
                                    QStringLiteral("_ugf"),    QStringLiteral("_onfoot_"), QStringLiteral("onfoot_"),
                                    QStringLiteral("_onfoot"), QStringLiteral("_foot_")};
    static const QStringList transport = {QStringLiteral("recovercargo"), QStringLiteral("cargo_recover"),
                                          QStringLiteral("salvage"), QStringLiteral("hauling"),
                                          QStringLiteral("freight")};
    const auto any = [&](const QStringList &tokens) {
        return std::any_of(tokens.begin(), tokens.end(), [&](const QString &t) { return key.contains(t); });
    };
    if (!any(fps))
        return QStringLiteral("Ship");
    return any(transport) ? QStringLiteral("FPS & Ship") : QStringLiteral("FPS");
}

bool isRouteTitle(const QString &titleKey)
{
    const QString low = titleKey.toLower();
    return low.contains(u"haulcargo") || low.contains(u"delivery") || low.contains(u"courier");
}

bool titleHasRouteToken(const QString &title)
{
    if (title.isEmpty())
        return false;
    for (const QRegularExpressionMatch &m : routeTokenRe().globalMatch(title))
        if (!routeTokenRole(m.captured(1)).isEmpty())
            return true;
    return false;
}

QString titleRouteToken(const QString &var, const QString &bodyToken, const QString &locationDetail)
{
    static const QRegularExpression canonical(QStringLiteral(R"(^(location|destination)\d*$)"),
                                              QRegularExpression::CaseInsensitiveOption);
    if (canonical.match(var).hasMatch()) {
        const QString mod = locationDetail == u"name" ? QStringLiteral("name") : QStringLiteral("Address");
        return QStringLiteral("~mission(%1|%2)").arg(var, mod);
    }
    return bodyToken;
}

std::pair<RouteTokens, RouteTokens> expandNestedRouteVars(const QString &var, const Loc &loc, RouteExpandCache *cache)
{
    if (cache)
        if (const auto it = cache->constFind(var); it != cache->cend())
            return *it;
    RouteTokens fromTokens, toTokens;
    if (!loc.isEmpty() && var.toLower().endsWith(u"token")) {
        const QString suffix = u'_' + var;
        std::vector<RouteTokens> perFrom, perTo;
        for (const auto &[key, text] : loc) {
            if (!key.endsWith(suffix))
                continue;
            RouteTokens from, to;
            for (const QRegularExpressionMatch &m : routeTokenRe().globalMatch(text)) {
                const QString v2 = m.captured(1);
                const QString role = routeTokenRole(v2);
                if (role.isEmpty())
                    continue;
                RouteTokens &side = role == u"from" ? from : to;
                if (!side.contains(v2))
                    side[v2] = tokenText(m);
            }
            perFrom.push_back(from);
            perTo.push_back(to);
        }
        // Strict: a variant without the var means it may not register.
        const auto strict = [](const std::vector<RouteTokens> &per) {
            RouteTokens out;
            if (per.empty())
                return out;
            for (const auto &[v, t] : per.front()) {
                bool all = true;
                for (std::size_t i = 1; i < per.size() && all; ++i)
                    all = per[i].contains(v);
                if (all)
                    out[v] = t;
            }
            return out;
        };
        fromTokens = strict(perFrom);
        toTokens = strict(perTo);
    }
    std::pair result{fromTokens, toTokens};
    if (cache)
        cache->insert(var, result);
    return result;
}

QString deriveRouteFragment(const QList<const QString *> &bodies, const tags::TagConfig *config, const Loc &loc,
                            RouteExpandCache *cache)
{
    const QString arrow = config ? config->routeArrow : QStringLiteral("gt");
    const QString detail = config ? config->locationDetail : QStringLiteral("address");
    std::vector<RouteTokens> perFrom, perTo;
    for (const QString *body : bodies) {
        if (!body || body->isEmpty())
            continue;
        RouteTokens from, to;
        for (const QRegularExpressionMatch &m : routeTokenRe().globalMatch(*body)) {
            const QString var = m.captured(1);
            const QString role = routeTokenRole(var);
            if (role == u"from") {
                if (!from.contains(var))
                    from[var] = tokenText(m);
            } else if (role == u"to") {
                if (!to.contains(var))
                    to[var] = tokenText(m);
            } else if (!m.hasCaptured(2)) {
                const auto [nestedFrom, nestedTo] = expandNestedRouteVars(var, loc, cache);
                for (const auto &[v, t] : nestedFrom)
                    if (!from.contains(v))
                        from[v] = t;
                for (const auto &[v, t] : nestedTo)
                    if (!to.contains(v))
                        to[v] = t;
            }
        }
        if (!from.isEmpty())
            perFrom.push_back(from);
        if (!to.isEmpty())
            perTo.push_back(to);
    }
    const RouteTokens fromTokens = agreedEndpointTokens(perFrom);
    const RouteTokens toTokens = agreedEndpointTokens(perTo);
    if (fromTokens.isEmpty() && toTokens.isEmpty())
        return QString();
    QStringList fromParts, toParts;
    for (const auto &[v, t] : fromTokens)
        fromParts << titleRouteToken(v, t, detail);
    for (const auto &[v, t] : toTokens)
        toParts << titleRouteToken(v, t, detail);
    return tags::renderRoute(fromParts.join(QStringLiteral(", ")), toParts.join(QStringLiteral(", ")), arrow,
                             fromTokens.size() > 1, toTokens.size() > 1);
}

Loc sizeAbbreviationOverrides(const Loc &loc, const QSet<QString> &shortenedSizes)
{
    Loc out;
    if (shortenedSizes.isEmpty())
        return out;
    QHash<QString, QString> abbrev;
    for (const auto &[word, shortForm] : tags::sizeAbbreviations())
        abbrev.insert(QString::fromUtf8(word), QString::fromUtf8(shortForm));
    for (const auto &[key, value] : loc) {
        if (!key.startsWith(QStringLiteral("HaulCargo_CargoGrade_")) && !key.startsWith(QStringLiteral("HaulCargo_CargoScale_")))
            continue;
        if (!shortenedSizes.contains(value))
            continue;
        if (const QString s = abbrev.value(value); !s.isEmpty())
            out.insert(key, s);
    }
    return out;
}

bool SpawnBreakdown::any() const
{
    return std::any_of(std::begin(buckets), std::end(buckets), [](const auto &b) { return !b.isEmpty(); });
}

std::pair<Spawn, QString> classifySpawnGroup(const QString &name, const QString &kind)
{
    const QString lower = name.toLower();
    for (const SpawnKeyword &k : kSpawnKeywords) {
        if (k.kind && kind != QLatin1StringView(k.kind))
            continue;
        if (lower.contains(QLatin1StringView(k.substring)))
            return {k.bucket, QString::fromLatin1(k.label)};
    }
    return {Spawn::Unknown, QStringLiteral("Unknown")};
}

SpawnBreakdown extractSpawnCounts(Node element, const QStringList &excludeWithin)
{
    SpawnBreakdown b;
    for (const Node sg : findAll(element, ".//SpawnDescription_ShipGroup")) {
        if (withinExcludedSubtree(sg, element, excludeWithin))
            continue;
        const QString name = qs(getOr(sg, "Name"));
        qint64 total = 0;
        for (const Node ship : findAll(sg, ".//SpawnDescription_Ship"))
            total += requireInt(getOr(ship, "concurrentAmount", "0"));
        if (total <= 0)
            continue;
        // Turrets are reported on their own line.
        if (name.toLower().contains(u"turret"))
            continue;
        auto [bucket, label] = classifySpawnGroup(name, QStringLiteral("ship"));
        if (bucket == Spawn::Unknown)
            if (const auto fallback = wrapperHostileFallback(sg))
                std::tie(bucket, label) = *fallback;
        addSpawn(b, bucket, label, total);
    }
    static const QRegularExpression countInName = py::re(QStringLiteral(R"(x\s*(\d+))"));
    for (const Node ng : findAll(element, ".//SpawnDescription_NPC_Group")) {
        if (withinExcludedSubtree(ng, element, excludeWithin))
            continue;
        const QString name = qs(getOr(ng, "Name"));
        qint64 npcs = 0;
        for (const Node autoSpawn : findAll(ng, ".//autoSpawnSettings")) {
            const std::string_view maxSpawns = getOr(autoSpawn, "maxSpawns", "0");
            if (maxSpawns != "-1") {
                npcs += std::max<qint64>(requireInt(maxSpawns), 0);
            } else {
                const std::string_view concurrent = getOr(autoSpawn, "maxConcurrentSpawns", "0");
                if (concurrent != "-1")
                    npcs += std::max<qint64>(requireInt(concurrent), 0);
            }
        }
        if (npcs <= 0)
            if (const QRegularExpressionMatch m = countInName.match(name); m.hasMatch())
                npcs = m.captured(1).toLongLong();
        if (npcs <= 0)
            continue;
        auto [bucket, label] = classifySpawnGroup(name, QStringLiteral("npc"));
        if (bucket == Spawn::Unknown)
            if (const auto fallback = wrapperHostileFallback(ng))
                std::tie(bucket, label) = *fallback;
        addSpawn(b, bucket, label, npcs);
    }
    return b;
}

QStringList formatSpawnLines(const SpawnBreakdown &breakdown)
{
    // Unknown is never shown (#187).
    QStringList lines;
    for (const auto &[bucket, header] : {std::pair{Spawn::Hostile, "Hostiles"},
                                         {Spawn::Friendly, "Friendlies"},
                                         {Spawn::Objective, "Objectives"}}) {
        const QMap<QString, qint64> &items = breakdown[bucket];
        if (items.isEmpty())
            continue;
        QStringList parts;
        for (auto it = items.cbegin(); it != items.cend(); ++it)
            parts << QStringLiteral("%1 x%2").arg(it.key()).arg(it.value());
        lines << QStringLiteral("<EM4>%1:</EM4> %2").arg(QString::fromLatin1(header), parts.join(QStringLiteral(", ")));
    }
    return lines;
}

void mergeSpawnBreakdownsMax(SpawnBreakdown &into, const SpawnBreakdown &src)
{
    for (int i = 0; i < 4; ++i)
        for (auto it = src.buckets[i].cbegin(); it != src.buckets[i].cend(); ++it)
            if (it.value() > into.buckets[i].value(it.key(), 0))
                into.buckets[i][it.key()] = it.value();
}

QString extractTurretInfo(Node root)
{
    qint64 turrets = 0;
    for (const Node sg : findAll(root, ".//SpawnDescription_ShipGroup")) {
        if (!qs(getOr(sg, "Name")).toLower().contains(u"turret"))
            continue;
        for (const Node ship : findAll(sg, ".//SpawnDescription_Ship"))
            turrets += requireInt(getOr(ship, "concurrentAmount", "0"));
    }
    std::optional<bool> hostile;
    for (const Node prop : findAll(root, ".//MissionProperty")) {
        if (getOr(prop, "missionVariableName") != "OverrideTurretHosility_BP") // CIG's spelling
            continue;
        if (const Node val = find(prop, ".//MissionPropertyValue_Boolean"))
            hostile = getOr(val, "value") == "1";
        break;
    }
    if (turrets == 0 && !hostile)
        return QString();
    const QString count = turrets > 0 ? QString::number(turrets) : QStringLiteral("present");
    return hostile == false ? count + QStringLiteral(" (friendly)") : count + QStringLiteral(" (hostile)");
}

int parseDifficultyRating(const QString &value)
{
    if (value.isEmpty())
        return 0;
    const qsizetype us = value.lastIndexOf(u'_');
    if (us < 0)
        return 0;
    const QString tail = value.sliced(us + 1);
    bool ok = !tail.isEmpty() && std::all_of(tail.begin(), tail.end(), [](QChar c) { return c.isDigit(); });
    return ok ? tail.toInt() : 0;
}

QString extractDifficulty(Node element)
{
    if (const Node diff = find(element, ".//ContractDifficulty")) {
        const int combat = parseDifficultyRating(qs(getOr(diff, "mechanicalSkill")));
        const int complexity = parseDifficultyRating(qs(getOr(diff, "mentalLoad")));
        const int risk = parseDifficultyRating(qs(getOr(diff, "riskOfLoss")));
        const int knowledge = parseDifficultyRating(qs(getOr(diff, "gameKnowledge")));
        if (combat || complexity || risk || knowledge) {
            QStringList parts;
            if (combat)
                parts << QStringLiteral("Combat %1/7").arg(combat);
            if (complexity)
                parts << QStringLiteral("Complexity %1/7").arg(complexity);
            if (risk)
                parts << QStringLiteral("Risk %1/7").arg(risk);
            if (knowledge)
                parts << QStringLiteral("Knowledge %1/7").arg(knowledge);
            return parts.join(QStringLiteral(" | "));
        }
    }
    const std::string_view diffVal = getOr(element, "missionDifficulty", "-1");
    if (!diffVal.empty() && diffVal != "-1")
        if (const std::optional<qint64> v = py::toInt(qs(diffVal)))
            return QStringLiteral("%1/7").arg(*v);
    return QString();
}

QString repRewardLine(const QString &fieldName, const QString &amount, const QString &repXpLabel, const QString &track)
{
    const QString suffix = !track.isEmpty() && track != fieldName ? QStringLiteral(" (%1)").arg(track) : QString();
    if (!fieldName.isEmpty() && fieldName != repXpLabel)
        return QStringLiteral("<EM4>%1:</EM4> %2 %3%4").arg(fieldName, amount, repXpLabel, suffix);
    return QStringLiteral("<EM4>%1:</EM4> %2%3").arg(repXpLabel, amount, suffix);
}

QStringList extractMissionFlags(Node root)
{
    QStringList flags;
    if (getOr(root, "linkedMission", kNullUuid) != kNullUuid)
        flags << QStringLiteral("Chain");
    if (getOr(root, "tutorial") == "1")
        flags << QStringLiteral("Starter");
    if (getOr(root, "onceOnly") == "1")
        flags << QStringLiteral("Unique");
    return flags;
}

qint64 extractMissionXp(Node root, const QHash<QString, qint64> &reputation)
{
    // The success outcome, primary faction only (as SCMDB shows it).
    const std::vector<Node> outcomes = findAll(root, ".//missionResultReputationRewards/SReputationAmountListParams");
    if (outcomes.empty())
        return 0;
    const std::vector<Node> amounts = findAll(outcomes.front(), ".//SReputationAmountParams");
    if (amounts.empty())
        return 0;
    const auto primary = get(amounts.front(), "reputationScope");
    qint64 total = 0;
    for (const Node amount : amounts) {
        if (get(amount, "reputationScope") != primary)
            continue;
        const std::string_view reward = getOr(amount, "reward");
        if (reward.empty())
            continue;
        if (const auto it = reputation.constFind(qs(reward)); it != reputation.cend())
            total += *it;
    }
    return total;
}

QString enhancementsMission(Node root, const QHash<QString, qint64> &reputation, const QString &repXpLabel,
                            const Context &ctx, const QSet<QString> *spawnAmbiguousKeys)
{
    QStringList lines;
    try {
        QString key = missionLocKey(root);
        if (key.isEmpty())
            key = locKey(root);
        lines << QStringLiteral("<EM4>Engagement Type:</EM4> ") + classifyMissionEngagement(key);

        const QStringList flags = extractMissionFlags(root);
        if (ctx.showField(QStringLiteral("mission_type")))
            lines << QStringLiteral("<EM4>Mission Type:</EM4> ") +
                         (flags.isEmpty() ? QStringLiteral("Standard") : flags.join(QStringLiteral(", ")));
        const QString difficulty = extractDifficulty(root);
        if (!difficulty.isEmpty() && ctx.showField(QStringLiteral("difficulty")))
            lines << QStringLiteral("<EM4>Difficulty (1-7):</EM4> ") + difficulty;
        const qint64 xp = extractMissionXp(root, reputation);
        if (xp > 0 && ctx.showField(QStringLiteral("reputation")))
            lines << QStringLiteral("<EM4>%1:</EM4> %2").arg(repXpLabel, py::integer(xp, true));

        // #165: a description shared by missions with different hostiles
        // can't show one count for them.
        const bool ambiguous = spawnAmbiguousKeys && spawnAmbiguousKeys->contains(key);
        if (ctx.showField(QStringLiteral("spawns"))) {
            SpawnBreakdown b = extractSpawnCounts(root);
            if (ambiguous)
                b[Spawn::Hostile].clear();
            lines << formatSpawnLines(b);
        }
        if (const QString turrets = extractTurretInfo(root); !turrets.isEmpty())
            lines << QStringLiteral("<EM4>Turrets:</EM4> ") + turrets;
    } catch (const PyError &) {
        // The Python swallows the error and keeps the lines it had.
    }
    return lines.join(kNl);
}

} // namespace core::enh
