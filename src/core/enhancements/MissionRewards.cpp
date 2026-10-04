#include "core/blueprints/BlueprintMeta.h"
#include "core/enhancements/Categories.h"
#include "core/enhancements/Missions.h"
#include "core/enhancements/Stats.h"
#include "core/text/PyFormat.h"
#include "core/text/PyText.h"

#include <QLoggingCategory>

#include <algorithm>
#include <map>

Q_DECLARE_LOGGING_CATEGORY(lcEnh)

namespace core::enh {

namespace {

bool lessList(const QStringList &a, const QStringList &b)
{
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                        [](const QString &x, const QString &y) { return py::less(x, y); });
}

QStringList pySortedUnique(QStringList list)
{
    std::sort(list.begin(), list.end(), [](const QString &a, const QString &b) { return py::less(a, b); });
    list.erase(std::unique(list.begin(), list.end()), list.end());
    return list;
}

std::string_view lstripAt(std::string_view s)
{
    while (!s.empty() && s.front() == '@')
        s.remove_prefix(1);
    return s;
}

QString paramValue(Node contract, std::string_view param)
{
    const Node p = find(contract, std::string(".//ContractStringParam[@param='") + std::string(param) + "']");
    return p ? qs(lstripAt(getOr(p, "value"))) : QString();
}

// Manual labels for pools CIG puts behind one shared description (#360),
// keyed by the pool's exact item set. English only by construction.
const std::vector<std::pair<QSet<QString>, QString>> &poolLabelOverrides()
{
    static const std::vector<std::pair<QSet<QString>, QString>> table = {
        {{QStringLiteral("P8-AR Rifle"), QStringLiteral("P8-AR Rifle Magazine (15 Cap)"),
          QStringLiteral("Palatino Arms"), QStringLiteral("Palatino Arms Moonfall"),
          QStringLiteral("Palatino Core"), QStringLiteral("Palatino Core Moonfall"),
          QStringLiteral("Palatino Helmet"), QStringLiteral("Palatino Helmet Moonfall"),
          QStringLiteral("Palatino Legs"), QStringLiteral("Palatino Legs Moonfall")},
         QStringLiteral("Yormandi Eyes")},
        {{QStringLiteral("Prism \"Bonedust\" Laser Shotgun"),
          QStringLiteral("Prism \"Deep Sea\" Laser Shotgun"),
          QStringLiteral("Prism \"Firesteel\" Laser Shotgun"), QStringLiteral("Prism Laser Shotgun"),
          QStringLiteral("Prism Laser Shotgun Battery (20 cap)"), QStringLiteral("Siebe Helmet"),
          QStringLiteral("Stirling Exploration Suit")},
         QStringLiteral("Irradiated Valakkar Pearls")},
    };
    return table;
}

QString poolLabelOverride(const QStringList &items, bool allowOverrides)
{
    if (!allowOverrides)
        return QString();
    const QSet<QString> key(items.begin(), items.end());
    for (const auto &[set, label] : poolLabelOverrides())
        if (set == key)
            return label;
    // A near miss means CIG changed a tracked pool: worth a maintainer's look.
    for (const auto &[set, label] : poolLabelOverrides()) {
        const QSet<QString> overlap = QSet<QString>(key).intersect(set);
        const qsizetype smaller = std::min(key.size(), set.size());
        if (!overlap.isEmpty() && smaller && double(overlap.size()) / double(smaller) >= 0.7)
            qCWarning(lcEnh) << "Blueprint pool closely resembles the" << label
                             << "override but isn't an exact match; check the override table";
    }
    return QString();
}

QString bulletList(const QStringList &items)
{
    QStringList lines;
    for (const QString &name : items)
        lines << QStringLiteral("- ") + name;
    return lines.join(kNl);
}

} // namespace

QString stripCigSizePrefix(const QString &name)
{
    static const QRegularExpression re = py::re(QStringLiteral(R"(^S(\d+)\s+)"));
    const QRegularExpressionMatch m = re.match(name);
    if (m.hasMatch() && m.capturedView(1).toLongLong() <= 20) // larger is part of a name ("S71 Rifle")
        return name.sliced(m.capturedEnd());
    return name;
}

QString poolRankLabel(const QString &poolName)
{
    if (poolName.isEmpty())
        return QString();
    static const QRegularExpression range(QStringLiteral(R"(rank(\d+)to(\d+))"),
                                          QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression single(QStringLiteral(R"(rank(\d+)(?!\d|to))"),
                                           QRegularExpression::CaseInsensitiveOption);
    if (const QRegularExpressionMatch m = range.match(poolName); m.hasMatch())
        return QStringLiteral("Rank %1–%2").arg(m.captured(1), m.captured(2));
    if (const QRegularExpressionMatch m = single.match(poolName); m.hasMatch())
        return QStringLiteral("Rank %1").arg(m.captured(1));
    return QString();
}

QString nameFromBlueprintFilename(const QString &path)
{
    static const QHash<QString, QString> aliases = {
        {QStringLiteral("Nozzle Fuelgiver Grin Nozzlefast"), QStringLiteral("Norfield")},
        {QStringLiteral("Nozzle Fuelgiver Grin Nozzlesecure"), QStringLiteral("Marlin")},
        {QStringLiteral("Nozzle Fuelgiver Grin Nozzleveryfast"), QStringLiteral("Lindstrom")},
        {QStringLiteral("Nozzle Fuelgiver Grin Nozzleverysecure"), QStringLiteral("Harkin")},
        {QStringLiteral("Nozzle Fuelgiver Misc Nozzlestandard"), QStringLiteral("RN-7s")},
        {QStringLiteral("Nozzle Fuelgiver Shin Nozzleexpensivefast"), QStringLiteral("Bendix")},
        {QStringLiteral("Nozzle Fuelgiver Shin Nozzleexpensivesecure"), QStringLiteral("Torrez")},
        {QStringLiteral("Nozzle Fuelgiver Shin Nozzlemostexpensive"), QStringLiteral("Ezra")},
    };
    QString stem = blueprints::stripRawBlueprintFilenamePrefix(fileStem(path)).first;
    stem.replace(u'_', u' ').replace(u'-', u' ');
    const QString fallback = py::title(stem);
    return aliases.value(fallback, fallback);
}

BlueprintPools buildBlueprintPoolLookup(const RecordStore &store, const QHash<QString, QString> &entityNames,
                                        const QHash<QString, QString> &entityNamesByFilename,
                                        const QHash<QString, QString> &entityNameTags,
                                        const QString &placement,
                                        const QHash<QString, QString> &nameFallbackTags)
{
    BlueprintPools out;
    const QString poolDir = QStringLiteral("crafting/blueprintrewards");
    const QString bpDir = QStringLiteral("crafting/blueprints/crafting");
    if (!store.dirExists(poolDir) || !store.dirExists(bpDir))
        return out;

    struct Blueprint
    {
        QString entityClass;
        QString stem;     // lower-cased, bp_ prefix removed
        QString fallback; // filename-derived name
    };
    QHash<QString, Blueprint> byRef;
    for (const auto files = store.rglob(bpDir); const QString &file : files) {
        const XmlDoc doc = XmlDoc::load(file);
        if (!doc)
            continue;
        const QString ref = qs(getOr(doc.root(), "__ref"));
        if (ref.isEmpty())
            continue;
        QString entityClass;
        forEachElement(doc.root(), [&](Node el) {
            if (polyType(el) != "CraftingProcess_Creation")
                return true;
            entityClass = qs(getOr(el, "entityClass"));
            return false;
        });
        QString stem = fileStem(file);
        for (const char *prefix : {"bp_craft_", "bp_rewards_", "bp_"})
            if (stem.startsWith(QLatin1StringView(prefix))) {
                stem = stem.sliced(int(std::strlen(prefix)));
                break;
            }
        byRef.insert(ref, {entityClass, stem.toLower(), nameFromBlueprintFilename(file)});
    }

    for (const auto files = store.rglob(poolDir); const QString &file : files) {
        const XmlDoc doc = XmlDoc::load(file);
        if (!doc)
            continue;
        const QString poolId = qs(getOr(doc.root(), "__ref"));
        if (poolId.isEmpty())
            continue;
        QStringList names;
        for (const Node reward : iter(doc.root(), "BlueprintReward")) {
            const QString bpRef = qs(getOr(reward, "blueprintRecord"));
            const auto bp = byRef.constFind(bpRef);
            if (bpRef.isEmpty() || bp == byRef.cend())
                continue;
            // The entity's own name by UUID, else by file stem, else the
            // file name made readable.
            QString name;
            if (const auto it = entityNames.constFind(bp->entityClass); it != entityNames.cend())
                name = stripCigSizePrefix(*it);
            else if (const auto it2 = entityNamesByFilename.constFind(bp->stem);
                     it2 != entityNamesByFilename.cend())
                name = stripCigSizePrefix(*it2);
            else
                name = bp->fallback;
            QString tag = entityNameTags.value(bp->entityClass);
            if (tag.isEmpty())
                tag = nameFallbackTags.value(name);
            if (!tag.isEmpty())
                name = tags::joinTag(name, tag, placement);
            if (!name.isEmpty() && !names.contains(name))
                names << name;
        }
        if (names.isEmpty())
            continue;
        std::sort(names.begin(), names.end(),
                  [](const QString &a, const QString &b) { return py::less(a, b); });
        out.items.insert(poolId, names);
        QString stem = fileStem(file).toLower();
        for (const char *prefix : {"bp_rewards_", "bp_"})
            if (stem.startsWith(QLatin1StringView(prefix))) {
                stem = stem.sliced(int(std::strlen(prefix)));
                break;
            }
        out.names.insert(poolId, stem);
    }
    return out;
}

QStringList buildBlueprintBodyParts(const FingerprintMap &uniqueFps, bool allowOverrides)
{
    if (uniqueFps.size() == 1) {
        const auto &[items, keys] = uniqueFps.front();
        if (const QString override = poolLabelOverride(items, allowOverrides); !override.isEmpty())
            return {QStringLiteral("<EM4>[%1]</EM4>").arg(override) + kNl + bulletList(items)};
        QStringList labels, systems;
        for (const auto &[sys, label] : keys) {
            if (!label.isEmpty())
                labels << label;
            systems << sys;
        }
        labels = pySortedUnique(labels);
        if (!labels.isEmpty()) {
            const QString header = pySortedUnique(systems).join(QStringLiteral(", ")) + QStringLiteral(", ") +
                                   labels.join(QStringLiteral(", "));
            return {QStringLiteral("<EM4>[%1]</EM4>").arg(header) + kNl + bulletList(items)};
        }
        return {bulletList(items)};
    }

    struct Entry
    {
        QString header;
        QStringList fp;
        std::vector<std::pair<QString, QString>> sortedKeys;
    };
    std::vector<Entry> entries;
    for (const auto &[fp, keys] : uniqueFps) {
        std::vector<std::pair<QString, QString>> sortedKeys = keys;
        std::sort(sortedKeys.begin(), sortedKeys.end(), [](const auto &a, const auto &b) {
            return a.first != b.first ? py::less(a.first, b.first) : py::less(a.second, b.second);
        });
        if (const QString override = poolLabelOverride(fp, allowOverrides); !override.isEmpty()) {
            entries.push_back({override, fp, sortedKeys});
            continue;
        }
        QStringList systems, labels;
        for (const auto &[sys, label] : keys) {
            systems << sys;
            if (!label.isEmpty())
                labels << label;
        }
        const QString sys = pySortedUnique(systems).join(QStringLiteral(", "));
        labels = pySortedUnique(labels);
        entries.push_back(
            {labels.isEmpty() ? sys : sys + QStringLiteral(", ") + labels.join(QStringLiteral(", ")), fp,
             sortedKeys});
    }
    // By (system/label keys, header, fingerprint): deterministic whatever
    // order the contracts were scanned in.
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        const bool keysLess = std::lexicographical_compare(
            a.sortedKeys.begin(), a.sortedKeys.end(), b.sortedKeys.begin(), b.sortedKeys.end(),
            [](const auto &x, const auto &y) {
                return x.first != y.first ? py::less(x.first, y.first) : py::less(x.second, y.second);
            });
        const bool keysGreater = std::lexicographical_compare(
            b.sortedKeys.begin(), b.sortedKeys.end(), a.sortedKeys.begin(), a.sortedKeys.end(),
            [](const auto &x, const auto &y) {
                return x.first != y.first ? py::less(x.first, y.first) : py::less(x.second, y.second);
            });
        if (keysLess != keysGreater)
            return keysLess;
        if (a.header != b.header)
            return py::less(a.header, b.header);
        return lessList(a.fp, b.fp);
    });

    // Colliding headers are told apart by their first item, then a number.
    QHash<QString, int> headerCounts;
    for (const Entry &e : entries)
        ++headerCounts[e.header];
    std::vector<std::pair<QString, QStringList>> named;
    for (const Entry &e : entries) {
        QString header = e.header;
        if (headerCounts.value(header) > 1)
            header = QStringLiteral("%1, %2 Set")
                         .arg(header, e.fp.isEmpty() ? QStringLiteral("Unknown") : e.fp.front());
        named.emplace_back(header, e.fp);
    }
    QHash<QString, int> namedCounts, seen;
    for (const auto &[header, fp] : named)
        ++namedCounts[header];
    QStringList parts;
    for (const auto &[header, fp] : named) {
        QString h = header;
        if (namedCounts.value(header) > 1)
            h = QStringLiteral("%1 (%2)").arg(header).arg(++seen[header]);
        parts << QStringLiteral("<EM4>[%1]</EM4>").arg(h) + kNl + bulletList(fp);
    }
    return parts;
}

// ── resource signatures ──────────────────────────────────────────────────

namespace {

// Curated per-ore base RS values (not in DataForge).
const OrderedMap<int> &mineableRsValues()
{
    static const OrderedMap<int> table = [] {
        OrderedMap<int> t;
        const std::pair<const char *, int> values[] = {
            {"agricium", 3885}, {"aluminium", 4285},     {"aslarite", 3840},   {"beryl", 3540},
            {"bexalite", 3600}, {"borase", 3570},        {"copper", 4240},     {"corundum", 4225},
            {"gold", 3585},     {"hephaestanite", 4180}, {"ice", 4300},        {"iron", 4270},
            {"laranite", 3825}, {"lindinium", 3400},     {"ouratite", 3370},   {"quantainium", 3170},
            {"quartz", 4210},   {"riccite", 3385},       {"savrillium", 3200}, {"silicon", 4255},
            {"stileron", 3185}, {"taranite", 3555},      {"tin", 4195},        {"titanium", 3855},
            {"torite", 3900},   {"tungsten", 3870},
        };
        for (const auto &[ore, value] : values)
            t[QString::fromLatin1(ore)] = value;
        return t;
    }();
    return table;
}

} // namespace

std::vector<int> rsValueSteps(const QString &ore)
{
    static const QHash<QString, std::vector<int>> steps = {
        {QStringLiteral("savrillium"), {3200, 6400}},
        {QStringLiteral("lindinium"), {3400, 6800, 10200}},
        {QStringLiteral("bexalite"), {3600, 7200, 10800, 14400}},
        {QStringLiteral("torite"), {3900, 7800, 11700, 15600, 19500}},
        {QStringLiteral("iron"), {4270, 8540, 12810, 17080, 21350, 25620}},
        {QStringLiteral("aluminium"), {4285, 8570, 12855, 17140, 21425, 25710}},
        {QStringLiteral("ice"), {4300, 8600, 12900, 17200, 21500, 25800}},
    };
    if (const auto it = steps.constFind(ore); it != steps.cend())
        return *it;
    if (const int *v = mineableRsValues().find(ore))
        return {*v};
    return {};
}

QStringList formatRsDetailsLines(const QStringList &ores, const Loc &loc)
{
    QStringList lines;
    for (const QString &ore : ores) {
        const std::vector<int> steps = rsValueSteps(ore);
        if (steps.empty())
            continue;
        const QString *label = loc.find(QStringLiteral("mineabletype_primary_") + ore);
        QStringList values;
        for (const int v : steps)
            values << QString::number(v);
        lines << QStringLiteral("<EM4>%1</EM4>: %2")
                     .arg(label ? *label : py::title(ore), values.join(QStringLiteral(" - ")));
    }
    if (lines.isEmpty())
        return {};
    lines.prepend(QStringLiteral("<EM4>Resource Signatures:</EM4>"));
    return lines;
}

Loc mineableRsNameOverrides(const Loc &loc)
{
    Loc out;
    for (const auto &[ore, value] : mineableRsValues()) {
        const QString key = QStringLiteral("mineabletype_primary_") + ore;
        if (const QString display = loc.value(key); !display.isEmpty())
            out.insert(key, QStringLiteral("%1 (RS %2)").arg(display).arg(value));
    }
    return out;
}

QString formatRsTag(const QStringList &ores)
{
    QStringList values;
    for (const QString &ore : ores)
        if (const int *v = mineableRsValues().find(ore))
            values << QString::number(*v);
    return values.isEmpty() ? QString() : QStringLiteral("[RS %1]").arg(values.join(u'/'));
}

// ── contract generators ─────────────────────────────────────────────────

namespace {

struct TemplateKeys
{
    QString title;
    QString desc;
};

QHash<QString, TemplateKeys> buildTemplateLookup(const RecordStore &store)
{
    QHash<QString, TemplateKeys> lookup;
    const QString dir = QStringLiteral("contracts/contracttemplates");
    if (!store.dirExists(dir))
        return lookup;
    for (const auto files = store.indexRglob(dir); const QString &file : files) {
        const XmlDoc doc = XmlDoc::load(file);
        if (!doc)
            continue;
        const QString ref = qs(getOr(doc.root(), "__ref"));
        if (ref.isEmpty())
            continue;
        QString title, desc;
        for (const Node lid : findAll(doc.root(), ".//LocID")) {
            const QString val = qs(getOr(lid, "value"));
            if (val.isEmpty() || !val.startsWith(u'@') || val.contains(u"LOC_EMPTY") ||
                val.contains(u"UNINITIALIZED"))
                continue;
            QString key = val;
            while (key.startsWith(u'@'))
                key.remove(0, 1);
            if (key.contains(u"_title", Qt::CaseInsensitive) && title.isEmpty())
                title = key;
            else if (key.contains(u"_desc", Qt::CaseInsensitive) && desc.isEmpty())
                desc = key;
        }
        if (!title.isEmpty())
            lookup.insert(ref, {title, desc});
    }
    return lookup;
}

} // namespace

QStringList battagliaContractOres(Node contract)
{
    static const QRegularExpression token(
        QRegularExpression::anchoredPattern(QStringLiteral("ResourceType[0-9]*")));
    QStringList ores;
    for (const Node prop : findAll(contract, ".//propertyOverrides/MissionProperty")) {
        if (!token.match(qs(getOr(prop, "extendedTextToken"))).hasMatch())
            continue;
        const Node opt = find(prop, ".//MissionPropertyValueOption_StringHash");
        if (!opt)
            continue;
        const QString textId = qs(getOr(opt, "textId"));
        const QString prefix = QStringLiteral("@mineabletype_primary_");
        if (!textId.startsWith(prefix))
            continue;
        const QString ore = textId.sliced(prefix.size());
        if (!ores.contains(ore))
            ores << ore;
    }
    return ores;
}

namespace {

// Concatenated .//CareerContract and .//Contract matches.
std::vector<Node> allContracts(Node root)
{
    std::vector<Node> contracts = findAll(root, ".//CareerContract");
    const std::vector<Node> plain = findAll(root, ".//Contract");
    contracts.insert(contracts.end(), plain.begin(), plain.end());
    return contracts;
}

struct RsTags
{
    QHash<QString, QString> titleTags;    // title key -> "[RS ####/...]"
    QHash<QString, QStringList> descOres; // desc key -> ores
};

RsTags buildBattagliaRsTags(const RecordStore &store, const QHash<QString, TemplateKeys> &templates)
{
    RsTags out;
    const QString dir = QStringLiteral("contracts/contractgenerator");
    if (!store.dirExists(dir))
        return out;
    for (const auto files = store.indexRglob(dir); const QString &file : files) {
        const XmlDoc doc = XmlDoc::load(file);
        if (!doc)
            continue;
        for (const Node contract : allContracts(doc.root())) {
            if (!find(contract, ".//ContractStringParam[@param='Title']"))
                continue;
            const QString titleKey = paramValue(contract, "Title");
            if (!titleKey.startsWith(QStringLiteral("Battaglia_RPT_Scan_")) &&
                !titleKey.startsWith(QStringLiteral("Battaglia_RPT_ScanMine_")))
                continue;
            QString descKey = paramValue(contract, "Description");
            if (descKey.isEmpty()) {
                const QString tmpl = qs(getOr(contract, "template"));
                if (!tmpl.isEmpty())
                    if (const auto it = templates.constFind(tmpl); it != templates.cend())
                        descKey = it->desc;
            }
            if (isSentinelKey(descKey))
                descKey.clear();
            const bool needTitle = !out.titleTags.contains(titleKey);
            const bool needDesc = !descKey.isEmpty() && !out.descOres.contains(descKey);
            if (!needTitle && !needDesc)
                continue;
            const QStringList ores = battagliaContractOres(contract);
            if (needTitle)
                if (const QString tag = formatRsTag(ores); !tag.isEmpty())
                    out.titleTags.insert(titleKey, tag);
            if (needDesc && std::any_of(ores.begin(), ores.end(),
                                        [](const QString &o) { return !rsValueSteps(o).empty(); }))
                out.descOres.insert(descKey, ores);
        }
    }
    return out;
}

// The region or distinguishing token of a contract debugName.
QString variantLabelShort(const QString &debugName)
{
    if (debugName.isEmpty())
        return QString();
    for (const char *prefix :
         {"BountyHuntersGuild_Bounties_", "BountyHuntersGuild_Bounty_", "BountyHuntersGuild_"})
        if (debugName.startsWith(QLatin1StringView(prefix)))
            return debugName.sliced(int(std::strlen(prefix))).section(u'_', 0, 0);
    return debugName.section(u'_', -1);
}

struct ContractVariant
{
    QString systemName;
    qint64 successXp = 0;
    qint64 failureXp = 0;
    QString descKey;
    QStringList flags;
    SpawnBreakdown spawns;
    QString difficulty;
    bool hasBp = false;
    double bpChance = 0;
    QString bpVariant;
    QString rankName;
    QString repTrack;
};

// title -> system -> pool identity -> (label, items).
struct PoolEntry
{
    QString label;
    QStringList items;
};
using PoolsBySystem = OrderedMap<OrderedMap<PoolEntry>>;

void mergeBlueprintPool(OrderedMap<PoolsBySystem> &missionBlueprints, const QString &titleKey,
                        const QString &system, const QString &poolKey, const QStringList &items,
                        const QString &label)
{
    OrderedMap<PoolEntry> &perPool = missionBlueprints[titleKey][system];
    if (!perPool.contains(poolKey))
        perPool[poolKey].label = label;
    PoolEntry &entry = perPool[poolKey];
    for (const QString &item : items)
        if (!entry.items.contains(item))
            entry.items << item;
}

struct ContractScan
{
    OrderedMap<std::vector<ContractVariant>> missions;
    OrderedMap<PoolsBySystem> blueprints;
    QHash<QString, double> bpChance;
    QHash<QString, QStringList> items;
};

QString extractSystem(const QString &name, const QString &fallback)
{
    static const QSet<QString> systems = {QStringLiteral("Stanton"), QStringLiteral("Pyro"),
                                          QStringLiteral("Nyx"),     QStringLiteral("Desert"),
                                          QStringLiteral("ArcCorp"), QStringLiteral("Crusader")};
    static const QRegularExpression region(QStringLiteral(R"(^Region[A-Z][0-9]*$)"));
    if (name.isEmpty())
        return fallback;
    QString sys, reg;
    for (const auto tokens = name.split(u'_'); const QString &token : tokens) {
        const QStringList sub = token.split(u'/');
        if (std::all_of(sub.begin(), sub.end(), [](const QString &s) { return systems.contains(s); })) {
            sys = token;
            reg.clear();
        } else if (!sys.isEmpty() && region.match(token).hasMatch()) {
            reg = token;
        }
    }
    if (sys.isEmpty())
        return fallback;
    return reg.isEmpty() ? sys : sys + u' ' + reg;
}

ContractScan scanContractGenerators(const Context &ctx, const BlueprintPools &pools,
                                    const QHash<QString, TemplateKeys> &templates)
{
    ContractScan scan;
    const QString dir = QStringLiteral("contracts/contractgenerator");
    if (!ctx.store->dirExists(dir))
        return scan;
    const QHash<QString, QString> &entityNames = ctx.scitem.entityNames;
    const QString nullUuid = qs(kNullUuid);

    try {
        for (const auto files = ctx.store->indexRglob(dir); const QString &file : files) {
            const XmlDoc doc = XmlDoc::load(file);
            if (!doc)
                continue;
            for (const auto &[handlerPath, contractPath] :
                 {std::pair{".//ContractGeneratorHandler_Career", ".//CareerContract"},
                  {".//ContractGeneratorHandler_List", ".//Contract"}}) {
                for (const Node handler : findAll(doc.root(), handlerPath)) {
                    if (getOr(handler, "notForRelease") == "1")
                        continue;
                    const QString debugName = qs(getOr(handler, "debugName"));
                    const QString handlerSystem =
                        extractSystem(debugName, debugName.isEmpty() ? QStringLiteral("Unknown") : debugName);
                    QStringList handlerFlags;
                    if (const Node da = find(handler, ".//defaultAvailability");
                        da && getOr(da, "onceOnly") == "1")
                        handlerFlags << QStringLiteral("Unique");
                    if (!findAll(handler, ".//ContractPrerequisite_CompletedContractTags").empty())
                        handlerFlags << QStringLiteral("Chain");
                    // Handler-scope spawns only, not the union of every
                    // child contract's roster (#186).
                    const SpawnBreakdown handlerSpawns = extractSpawnCounts(
                        handler, {QStringLiteral("CareerContract"), QStringLiteral("Contract")});

                    for (const Node contract : findAll(handler, contractPath)) {
                        try {
                            if (getOr(contract, "notForRelease") == "1")
                                continue;
                            const QString contractName = qs(getOr(contract, "debugName"));
                            const QString system = extractSystem(contractName, handlerSystem);
                            QString titleKey = paramValue(contract, "Title");
                            QString descKey = paramValue(contract, "Description");
                            if (titleKey.isEmpty() || descKey.isEmpty()) {
                                const QString tmpl = qs(getOr(contract, "template"));
                                if (!tmpl.isEmpty())
                                    if (const auto it = templates.constFind(tmpl); it != templates.cend()) {
                                        if (titleKey.isEmpty())
                                            titleKey = it->title;
                                        if (descKey.isEmpty())
                                            descKey = it->desc;
                                    }
                            }
                            // Never augment CIG's sentinel keys: their text
                            // shows wherever a reference fails to bind.
                            if (isSentinelKey(titleKey))
                                titleKey.clear();
                            if (isSentinelKey(descKey))
                                descKey.clear();
                            if (titleKey.isEmpty())
                                continue;

                            // This contract's pools, grouped by rank label;
                            // keyed below by its own pool set (#360).
                            bool hasBp = false;
                            double chance = 0;
                            const QString bpVariant = contractName;
                            OrderedMap<std::pair<QStringList, QStringList>>
                                byLabel; // label -> (uuids, items)
                            for (const Node bpElem : iter(contract, "BlueprintRewards")) {
                                const QString poolId = qs(getOr(bpElem, "blueprintPool"));
                                if (poolId.isEmpty() || poolId == nullUuid || !pools.items.contains(poolId))
                                    continue;
                                hasBp = true;
                                const QString label = poolRankLabel(pools.names.value(poolId));
                                auto &[uuids, labelItems] = byLabel[label];
                                uuids << poolId;
                                for (const auto items = pools.items.value(poolId);
                                     const QString &item : items)
                                    if (!labelItems.contains(item))
                                        labelItems << item;
                                const std::optional<double> c = toFloat(getOr(bpElem, "chance", "1"));
                                chance = c ? *c : 1.0;
                                if (!scan.bpChance.contains(titleKey))
                                    scan.bpChance.insert(titleKey, chance);
                            }
                            const auto poolKey = [](QStringList uuids) {
                                std::sort(uuids.begin(), uuids.end(),
                                          [](const QString &a, const QString &b) { return py::less(a, b); });
                                return uuids.join(u'\x1f');
                            };
                            for (const auto &[label, value] : byLabel)
                                mergeBlueprintPool(scan.blueprints, titleKey, system, poolKey(value.first),
                                                   value.second, label);

                            if (!entityNames.isEmpty()) {
                                QStringList itemNames;
                                for (const char *path : {".//ContractResult_Item", ".//ItemAwardEntityClass"})
                                    for (const Node itemElem : findAll(contract, path)) {
                                        const QString ec = qs(getOr(itemElem, "entityClass"));
                                        if (ec.isEmpty() || ec == nullUuid)
                                            continue;
                                        if (const auto it = entityNames.constFind(ec);
                                            it != entityNames.cend())
                                            if (!itemNames.contains(*it))
                                                itemNames << *it;
                                    }
                                if (!itemNames.isEmpty() && !scan.items.contains(titleKey))
                                    scan.items.insert(titleKey, itemNames);
                            }

                            // XP: the first positive legacy reward is success,
                            // the first negative failure.
                            qint64 successXp = 0, failureXp = 0;
                            for (const Node legacy :
                                 findAll(contract, ".//ContractResult_LegacyReputation")) {
                                const Node amount = find(legacy, "contractResultReputationAmounts");
                                if (!amount)
                                    continue;
                                const QString reward = qs(getOr(amount, "reward"));
                                if (reward.isEmpty())
                                    continue;
                                const auto it = ctx.reputation.constFind(reward);
                                if (it == ctx.reputation.cend())
                                    continue;
                                if (*it > 0 && successXp == 0)
                                    successXp = *it;
                                else if (*it < 0 && failureXp == 0)
                                    failureXp = *it;
                            }
                            if (successXp == 0) {
                                for (const Node sp :
                                     findAll(contract, ".//ContractResult_ScenarioProgress")) {
                                    const std::string_view points = getOr(sp, "PointsToAward");
                                    if (points.empty())
                                        continue;
                                    const Node first = find(sp, "./missionResults/Bool");
                                    if (!first || getOr(first, "value") != "1")
                                        continue;
                                    const std::optional<double> v = toFloat(points);
                                    if (!v || !std::isfinite(*v))
                                        continue;
                                    const qint64 val = qint64(std::trunc(*v));
                                    if (val > 0) {
                                        successXp = val;
                                        break;
                                    }
                                }
                            }

                            QStringList flags = handlerFlags;
                            if (contractName.contains(QStringLiteral("Intro")) ||
                                contractName.contains(QStringLiteral("intro")))
                                if (!flags.contains(QStringLiteral("Starter")))
                                    flags << QStringLiteral("Starter");
                            const SpawnBreakdown contractSpawns = extractSpawnCounts(contract);
                            const SpawnBreakdown &spawns =
                                contractSpawns.any() ? contractSpawns : handlerSpawns;
                            const QString difficulty = extractDifficulty(contract);
                            const QString minStanding = qs(getOr(contract, "minStanding"));
                            const QString rankName =
                                minStanding.isEmpty() ? QString() : ctx.standings.ranks.value(minStanding);
                            const QString repTrack =
                                minStanding.isEmpty() ? QString() : ctx.standings.tracks.value(minStanding);

                            scan.missions[titleKey].push_back({system, successXp, failureXp, descKey, flags,
                                                               spawns, difficulty, hasBp, chance, bpVariant,
                                                               rankName, repTrack});

                            // Sub-contracts override title/desc only.
                            for (const Node sub : findAll(contract, ".//subContracts/SubContract")) {
                                const QString subTitle = paramValue(sub, "Title");
                                QString subDesc = paramValue(sub, "Description");
                                if (subTitle.isEmpty() || isSentinelKey(subTitle))
                                    continue;
                                if (isSentinelKey(subDesc))
                                    subDesc.clear();
                                scan.missions[subTitle].push_back({system, successXp, failureXp,
                                                                   subDesc.isEmpty() ? descKey : subDesc,
                                                                   flags, spawns, difficulty, hasBp, chance,
                                                                   bpVariant, rankName, repTrack});
                                if (hasBp && subTitle != titleKey) {
                                    for (const auto &[label, value] : byLabel)
                                        mergeBlueprintPool(scan.blueprints, subTitle, system,
                                                           poolKey(value.first), value.second, label);
                                    if (!scan.bpChance.contains(subTitle))
                                        scan.bpChance.insert(subTitle, chance);
                                }
                            }
                        } catch (const PyError &) {
                            // One malformed contract is skipped, as in the Python.
                        }
                    }
                }
            }
        }
    } catch (const PyError &e) {
        qCWarning(lcEnh) << "Error scanning contract generators:" << e.what();
    }

    // Stanton first, then the rest alphabetically.
    for (auto &[title, variants] : scan.missions)
        std::stable_sort(variants.begin(), variants.end(),
                         [](const ContractVariant &a, const ContractVariant &b) {
                             const bool aNot = a.systemName != u"Stanton";
                             const bool bNot = b.systemName != u"Stanton";
                             if (aNot != bNot)
                                 return !aNot;
                             return py::less(a.systemName, b.systemName);
                         });
    return scan;
}

QString xpText(qint64 v)
{
    return py::integer(v, true);
}

} // namespace

Loc generateMissions(const Context &ctx)
{
    const RecordStore &store = *ctx.store;
    const Loc &loc = *ctx.loc;
    // Overrides are keyed on English names: only on an English run.
    const bool overridesOk = ctx.tagLoc == ctx.loc;
    const QString &repLabel = ctx.repXpLabel;
    const QString em = ctx.missionHeaderEm;
    const QString hdrDetails = ctx.missionHeader(QStringLiteral("details"));
    const QString hdrBlueprints = ctx.missionHeader(QStringLiteral("blueprints"));
    const QString hdrItems = ctx.missionHeader(QStringLiteral("items"));
    const QString bpHeaderTag = QStringLiteral("<%1>%2</%1>").arg(em, hdrBlueprints);
    const tags::TagConfig &compCfg = ctx.config(QStringLiteral("components"));
    const QHash<QString, QString> noTags;
    const QHash<QString, QString> &effectiveTags =
        ctx.annotateMissionDescs ? ctx.scitem.entityNameTags : noTags;
    const QHash<QString, QString> nameFallbackTags =
        ctx.annotateMissionDescs ? bareTypeNameTagLookup(loc, &compCfg) : QHash<QString, QString>{};
    const QString missionSep = QStringLiteral("\\n\\n<%1>%2</%1>\\n").arg(em, hdrDetails);

    Loc out;
    const QString puDir = QStringLiteral("missionbroker/pu_missions");
    const QStringList puFiles = store.dirExists(puDir) ? store.indexRglob(puDir) : QStringList();

    // #165: descriptions shared by missions with different hostile rosters.
    QSet<QString> spawnAmbiguous;
    {
        QHash<QString, QSet<QString>> signatures;
        for (const QString &file : puFiles) {
            const XmlDoc doc = XmlDoc::load(file);
            if (!doc)
                continue;
            const QString dk = missionLocKey(doc.root());
            if (dk.isEmpty())
                continue;
            try {
                const QMap<QString, qint64> host = extractSpawnCounts(doc.root())[Spawn::Hostile];
                QStringList sig;
                for (auto it = host.cbegin(); it != host.cend(); ++it)
                    sig << it.key() + QChar(0x1F) + QString::number(it.value());
                signatures[dk].insert(sig.join(QChar(0x1E)));
            } catch (const PyError &) {
            }
        }
        for (auto it = signatures.cbegin(); it != signatures.cend(); ++it)
            if (it->size() > 1)
                spawnAmbiguous.insert(it.key());
    }

    const EnhancementFn missionFn = [&](Node root) {
        return enhancementsMission(root, ctx.reputation, repLabel, ctx, &spawnAmbiguous);
    };
    ScanOptions options;
    options.separator = missionSep;
    options.captureAll = true;
    if (!puFiles.isEmpty()) {
        ScanOptions puOptions = options;
        puOptions.locKeyFn = missionLocKey;
        for (const auto &[k, v] : scanEntityDir(store, puDir, missionFn, loc, puOptions))
            out.insert(k, v);
    }
    for (const char *dir : {"entities/missions", "entities/contracts", "entities/jobterminal"}) {
        const QString rel = QString::fromLatin1(dir);
        if (store.dirExists(rel))
            for (const auto &[k, v] : scanEntityDir(store, rel, missionFn, loc, options))
                out.insert(k, v);
    }

    const BlueprintPools pools =
        buildBlueprintPoolLookup(store, ctx.scitem.entityNames, ctx.scitem.entityNamesByFilename,
                                 effectiveTags, compCfg.placement, nameFallbackTags);
    const QHash<QString, TemplateKeys> templates = buildTemplateLookup(store);
    ContractScan scan = scanContractGenerators(ctx, pools, templates);
    const RsTags rs = buildBattagliaRsTags(store, templates);

    // pu_missions titles -> their descriptions; haul descriptions (#102).
    QHash<QString, QSet<QString>> puTitleToDescs;
    QSet<QString> puCargoDeliveryDescs;
    for (const QString &file : puFiles) {
        const XmlDoc doc = XmlDoc::load(file);
        if (!doc)
            continue;
        const std::string_view titleAttr = getOr(doc.root(), "title");
        const std::string_view descAttr = getOr(doc.root(), "description");
        if (!titleAttr.starts_with('@') || !descAttr.starts_with('@'))
            continue;
        if (isSentinelLocRef(titleAttr) || isSentinelLocRef(descAttr))
            continue;
        const QString descKey = qs(lstripAt(descAttr));
        puTitleToDescs[qs(lstripAt(titleAttr))].insert(descKey);
        const QStringList parts = file.mid(store.recordsDir().size()).split(u'/', Qt::SkipEmptyParts);
        const qsizetype i = parts.indexOf(QStringLiteral("pu_missions"));
        if (i >= 0 && i + 1 < parts.size() && (parts[i + 1] == u"cargo" || parts[i + 1] == u"delivery"))
            puCargoDeliveryDescs.insert(descKey);
    }

    // Route bodies in a fixed order: the Python iterates a set here.
    const auto sortedDescs = [](const QSet<QString> &set) {
        QStringList list(set.cbegin(), set.cend());
        std::sort(list.begin(), list.end(),
                  [](const QString &a, const QString &b) { return py::less(a, b); });
        return list;
    };
    const auto bodies = [&](const QStringList &descKeys) {
        QList<const QString *> list;
        for (const QString &dk : descKeys)
            list << loc.find(dk);
        return list;
    };

    const tags::TagConfig &titlesCfg = ctx.config(QStringLiteral("mission_titles"));
    RouteExpandCache routeCache;
    for (const auto &[titleKey, variants] : scan.missions) {
        QString baseTitle = loc.value(titleKey);
        if (baseTitle.isEmpty())
            baseTitle = humanizeKey(titleKey);

        struct Tier
        {
            qint64 success, failure;
            QString rank, track;
            bool operator==(const Tier &) const = default;
        };
        std::vector<Tier> seenTiers;
        for (const ContractVariant &v : variants) {
            const Tier t{v.successXp, v.failureXp, v.rankName, v.repTrack};
            if (std::find(seenTiers.begin(), seenTiers.end(), t) == seenTiers.end())
                seenTiers.push_back(t);
        }
        std::vector<qint64> uniqueXp;
        for (const Tier &t : seenTiers)
            if (std::find(uniqueXp.begin(), uniqueXp.end(), t.success) == uniqueXp.end())
                uniqueXp.push_back(t.success);
        std::sort(uniqueXp.begin(), uniqueXp.end());
        QSet<QString> nonzeroTracks;
        for (const Tier &t : seenTiers)
            if (t.success > 0 && !t.track.isEmpty())
                nonzeroTracks.insert(t.track);
        const QString titleTrack = nonzeroTracks.size() == 1 ? *nonzeroTracks.cbegin() : QString();

        const bool hasBlueprints = scan.blueprints.contains(titleKey);
        const bool allHaveBp = hasBlueprints && std::all_of(variants.begin(), variants.end(),
                                                            [](const ContractVariant &v) { return v.hasBp; });
        const bool allGuaranteed =
            allHaveBp && std::all_of(variants.begin(), variants.end(),
                                     [](const ContractVariant &v) { return !v.hasBp || v.bpChance >= 1.0; });
        QSet<QString> cgDescKeys;
        for (const ContractVariant &v : variants)
            if (!v.descKey.isEmpty())
                cgDescKeys.insert(v.descKey);
        // Any PU cargo-delivery description of this title outside the CG ones.
        bool survivingNoBpCargo = false;
        for (const auto descs = puTitleToDescs.value(titleKey); const QString &d : descs)
            if (puCargoDeliveryDescs.contains(d) && !cgDescKeys.contains(d)) {
                survivingNoBpCargo = true;
                break;
            }

        OrderedMap<bool> bucketHasBp;
        QHash<QString, int> bucketCount;
        for (const ContractVariant &v : variants) {
            if (v.descKey.isEmpty())
                continue;
            bool &b = bucketHasBp[v.descKey];
            b = b || v.hasBp;
            ++bucketCount[v.descKey];
        }
        int totalBucketed = 0;
        for (const int c : std::as_const(bucketCount))
            totalBucketed += c;
        const bool anyVariantHasBp =
            std::any_of(variants.begin(), variants.end(), [](const ContractVariant &v) { return v.hasBp; });
        bool dominantNoBp = false;
        if (totalBucketed > 0)
            for (const auto &[dk, has] : bucketHasBp)
                dominantNoBp = dominantNoBp || (!has && double(bucketCount.value(dk)) / totalBucketed > 0.5);
        const bool bpPartial = hasBlueprints && anyVariantHasBp && !dominantNoBp;

        if (isRouteTitle(titleKey))
            baseTitle = tags::abbreviateTitle(baseTitle, titlesCfg.abbreviatedPhrases,
                                              titlesCfg.rankSeparator, titlesCfg.standardizeHaulingNames);
        QString title = baseTitle;
        if (tags::routeEnabled(&titlesCfg) && isRouteTitle(titleKey) && !titleHasRouteToken(baseTitle)) {
            QSet<QString> routeDescs = puTitleToDescs.value(titleKey);
            routeDescs.unite(cgDescKeys);
            const QString route =
                deriveRouteFragment(bodies(sortedDescs(routeDescs)), &titlesCfg, loc, &routeCache);
            if (!route.isEmpty())
                title = tags::applyMissionTitle(baseTitle, route, titlesCfg);
        }
        if (ctx.showTitleTag(QStringLiteral("blueprint"))) {
            if (allHaveBp && allGuaranteed && !survivingNoBpCargo)
                title += QStringLiteral(" <EM4>[BP]</EM4>");
            else if (bpPartial || (allHaveBp && (survivingNoBpCargo || !allGuaranteed)))
                title += QStringLiteral(" <EM4>[BP?]</EM4>");
        }
        if (ctx.showTitleTag(QStringLiteral("ace"))) {
            int aces = 0;
            for (const ContractVariant &v : variants)
                aces += v.spawns[Spawn::Hostile].contains(QStringLiteral("Ace Pilots")) ? 1 : 0;
            if (!variants.empty() && aces == int(variants.size()))
                title += QStringLiteral(" <EM4>[ACE]</EM4>");
            else if (aces > 0)
                title += QStringLiteral(" <EM4>[ACE?]</EM4>");
        }
        std::vector<qint64> nonzeroXp;
        if (ctx.showTitleTag(QStringLiteral("rep")))
            for (const qint64 x : uniqueXp)
                if (x > 0)
                    nonzeroXp.push_back(x);
        const QString trackSuffix = !titleTrack.isEmpty() && ctx.showTitleTag(QStringLiteral("rep_track"))
                                        ? QStringLiteral(" (%1)").arg(titleTrack)
                                        : QString();
        if (nonzeroXp.size() == 1)
            title +=
                QStringLiteral(" <EM4>[%1 %2%3]</EM4>").arg(xpText(nonzeroXp.front()), repLabel, trackSuffix);
        else if (nonzeroXp.size() > 1)
            title += QStringLiteral(" <EM4>[%1–%2 %3%4]</EM4>")
                         .arg(xpText(*std::min_element(nonzeroXp.begin(), nonzeroXp.end())),
                              xpText(*std::max_element(nonzeroXp.begin(), nonzeroXp.end())), repLabel,
                              trackSuffix);
        if (ctx.showTitleTag(QStringLiteral("rs")))
            if (const QString rsTag = rs.titleTags.value(titleKey); !rsTag.isEmpty())
                title += QStringLiteral(" <EM4>%1</EM4>").arg(rsTag);
        out.insert(titleKey, title);

        // #151: a description key equal to the title key gets no body.
        QStringList descKeys;
        for (const ContractVariant &v : variants)
            if (!v.descKey.isEmpty() && v.descKey != titleKey && !descKeys.contains(v.descKey))
                descKeys << v.descKey;

        for (const QString &descKey : std::as_const(descKeys)) {
            std::vector<const ContractVariant *> descVariants;
            for (const ContractVariant &v : variants)
                if (v.descKey == descKey)
                    descVariants.push_back(&v);
            QString baseDesc;
            if (const QString *stock = loc.find(descKey)) {
                baseDesc = *stock;
            } else {
                QString debug;
                for (const ContractVariant *v : descVariants)
                    if (!v->bpVariant.isEmpty()) {
                        debug = v->bpVariant;
                        break;
                    }
                baseDesc = humanizeKey(debug.isEmpty() ? descKey : debug);
            }

            QStringList allFlags, difficulties, bpVariantNames;
            SpawnBreakdown aggSpawns;
            bool allVariantsHaveBp = true;
            bool anyHasBp = false;
            double variantChance = 0;
            std::vector<Tier> descTiers;
            for (const ContractVariant *v : descVariants) {
                for (const QString &f : v->flags)
                    if (!allFlags.contains(f))
                        allFlags << f;
                mergeSpawnBreakdownsMax(aggSpawns, v->spawns);
                if (!v->difficulty.isEmpty() && !difficulties.contains(v->difficulty))
                    difficulties << v->difficulty;
                if (v->hasBp) {
                    anyHasBp = true;
                    variantChance = std::max(variantChance, v->bpChance);
                    const QString shortName = variantLabelShort(v->bpVariant);
                    if (!shortName.isEmpty() && !bpVariantNames.contains(shortName))
                        bpVariantNames << shortName;
                } else {
                    allVariantsHaveBp = false;
                }
                const Tier t{v->successXp, v->failureXp, v->rankName, v->repTrack};
                if (std::find(descTiers.begin(), descTiers.end(), t) == descTiers.end())
                    descTiers.push_back(t);
            }

            QStringList details;
            if (ctx.showField(QStringLiteral("mission_type")))
                details << QStringLiteral("<EM4>Mission Type:</EM4> ") +
                               (allFlags.isEmpty() ? QStringLiteral("Standard")
                                                   : allFlags.join(QStringLiteral(", ")));
            if (ctx.showField(QStringLiteral("difficulty")) && !difficulties.isEmpty())
                details << QStringLiteral("<EM4>Difficulty (1-7):</EM4> ") + difficulties.front();
            if (ctx.showField(QStringLiteral("resource_signatures")))
                if (const auto it = rs.descOres.constFind(descKey);
                    it != rs.descOres.cend() && !it->isEmpty())
                    details << formatRsDetailsLines(*it, loc);
            if (ctx.showField(QStringLiteral("spawns")))
                details << formatSpawnLines(aggSpawns);
            if (ctx.showField(QStringLiteral("ace")) &&
                aggSpawns[Spawn::Hostile].value(QStringLiteral("Ace Pilots"), 0))
                details << QStringLiteral("<EM4>Ace Pilot:</EM4> Yes");
            std::vector<Tier> nonzeroTiers;
            for (const Tier &t : descTiers)
                if (t.success > 0)
                    nonzeroTiers.push_back(t);
            if (ctx.showField(QStringLiteral("reputation"))) {
                if (nonzeroTiers.size() == 1) {
                    const Tier &t = nonzeroTiers.front();
                    details << repRewardLine(t.rank, xpText(t.success), repLabel, t.track);
                    if (t.failure < 0)
                        details << repRewardLine(QStringLiteral("Failure Penalty"), xpText(t.failure),
                                                 repLabel);
                } else if (nonzeroTiers.size() > 1) {
                    std::stable_sort(nonzeroTiers.begin(), nonzeroTiers.end(),
                                     [](const Tier &a, const Tier &b) { return a.success < b.success; });
                    int i = 0;
                    for (const Tier &t : nonzeroTiers) {
                        ++i;
                        QString line =
                            repRewardLine(t.rank.isEmpty() ? QStringLiteral("Tier %1").arg(i) : t.rank,
                                          xpText(t.success), repLabel, t.track);
                        if (t.failure < 0)
                            line += QStringLiteral(" (Failure: %1)").arg(xpText(t.failure));
                        details << line;
                    }
                }
            }

            QStringList sections = {baseDesc};
            if (anyHasBp && hasBlueprints && ctx.showField(QStringLiteral("blueprints"))) {
                const int pct = int(variantChance * 100); // truncates, as int() does
                QString bpHeader;
                if (allVariantsHaveBp)
                    bpHeader = pct < 100 ? QStringLiteral("<EM4>Blueprint Reward:</EM4> %1% chance").arg(pct)
                                         : QStringLiteral("<EM4>Blueprint Reward:</EM4> Guaranteed");
                else
                    bpHeader = QStringLiteral("<EM4>Blueprint Reward:</EM4> %1% chance (%2 only)")
                                   .arg(pct)
                                   .arg(bpVariantNames.isEmpty() ? QStringLiteral("select variants")
                                                                 : bpVariantNames.join(QStringLiteral(", ")));
                details << bpHeader;

                const PoolsBySystem &bySystem = *scan.blueprints.find(titleKey);
                QSet<QString> descSystems;
                for (const ContractVariant *v : descVariants)
                    if (v->hasBp)
                        descSystems.insert(v->systemName);
                bool anyDescSystem = false;
                for (const auto &[sys, byPool] : bySystem)
                    anyDescSystem = anyDescSystem || descSystems.contains(sys);
                FingerprintMap fps;
                for (const auto &[sys, byPool] : bySystem) {
                    if (anyDescSystem && !descSystems.contains(sys))
                        continue;
                    for (const auto &[poolKey, entry] : byPool) {
                        auto it = std::find_if(fps.begin(), fps.end(),
                                               [&](const auto &f) { return f.first == entry.items; });
                        if (it == fps.end()) {
                            fps.push_back({entry.items, {}});
                            it = fps.end() - 1;
                        }
                        it->second.emplace_back(sys, entry.label);
                    }
                }
                const QStringList bodyParts = buildBlueprintBodyParts(fps, overridesOk);
                const QString joiner = bodyParts.size() > 1 ? QStringLiteral("\\n\\n") : kNl;
                sections << bpHeaderTag + kNl + bodyParts.join(joiner);
            }
            if (const auto it = scan.items.constFind(titleKey); it != scan.items.cend()) {
                QStringList lines;
                for (const QString &name : *it)
                    lines << QStringLiteral("- ") + name;
                sections << QStringLiteral("<%1>%2</%1>").arg(em, hdrItems) + kNl + lines.join(kNl);
            }
            if (const QString block = details.join(kNl); !block.isEmpty())
                sections << QStringLiteral("<%1>%2</%1>").arg(em, hdrDetails) + kNl + block;
            if (anyHasBp && hasBlueprints && !allVariantsHaveBp &&
                ctx.showField(QStringLiteral("blueprints"))) {
                if (!bpVariantNames.isEmpty()) {
                    const QString quoted = bpVariantNames.join(QStringLiteral(", "));
                    sections << (bpVariantNames.size() == 1
                                     ? QStringLiteral("<EM4>? = only the %1 variant awards blueprints</EM4>")
                                           .arg(quoted)
                                     : QStringLiteral("<EM4>? = only the %1 variants award blueprints</EM4>")
                                           .arg(quoted));
                } else {
                    sections << QStringLiteral("<EM4>? = only some variants award blueprints</EM4>");
                }
            }

            const QString text = sections.join(QStringLiteral("\\n\\n"));
            const bool newHasBp = text.contains(bpHeaderTag);
            const QString *existing = out.find(descKey);
            if (!existing)
                out.insert(descKey, text);
            else if (newHasBp && !existing->contains(bpHeaderTag))
                out.insert(descKey, text); // a later title has the blueprints
        }
    }

    // Drop pu-only descriptions under BP titles, except hauls (#31, #102).
    for (const auto &[titleKey, variants] : scan.missions) {
        if (!scan.blueprints.contains(titleKey))
            continue;
        QSet<QString> cg;
        for (const ContractVariant &v : variants)
            if (!v.descKey.isEmpty())
                cg.insert(v.descKey);
        // A named copy: subtract() returns a reference into its object, and a
        // temporary in a range-for initializer dies before the loop body runs.
        QSet<QString> orphans = puTitleToDescs.value(titleKey);
        orphans.subtract(cg);
        for (const QString &orphan : std::as_const(orphans))
            if (!puCargoDeliveryDescs.contains(orphan))
                out.remove(orphan);
    }

    // XP for titles only pu_missions reach.
    OrderedMap<std::vector<qint64>> puTitleXps;
    for (const QString &file : puFiles) {
        const XmlDoc doc = XmlDoc::load(file);
        if (!doc)
            continue;
        const std::string_view titleAttr = getOr(doc.root(), "title");
        const std::string_view descAttr = getOr(doc.root(), "description");
        if (!titleAttr.starts_with('@') || !descAttr.starts_with('@'))
            continue;
        if (isSentinelLocRef(titleAttr) || isSentinelLocRef(descAttr))
            continue;
        const qint64 xp = extractMissionXp(doc.root(), ctx.reputation);
        if (xp > 0)
            puTitleXps[qs(lstripAt(titleAttr))].push_back(xp);
    }

    static const QRegularExpression xpTag = py::re(
        QStringLiteral("<EM4>\\[\\d[\\d,]*(?:[–\\-]\\d[\\d,]*)?\\s*\\w+(?:\\s*\\([^)]*\\))?\\]</EM4>"));
    if (!titlesCfg.shortenedSizes.isEmpty())
        for (const auto &[k, v] : sizeAbbreviationOverrides(loc, titlesCfg.shortenedSizes))
            out.insert(k, v);
    for (const auto &[titleKey, xps] : puTitleXps) {
        QString baseTitle = loc.value(titleKey);
        if (baseTitle.isEmpty())
            baseTitle = humanizeKey(titleKey);
        const QString *existing = out.find(titleKey);
        QString current = existing ? *existing : baseTitle;
        if (xpTag.match(current).hasMatch())
            continue;
        const bool fresh = existing == nullptr;
        if (fresh && isRouteTitle(titleKey))
            current = tags::abbreviateTitle(current, titlesCfg.abbreviatedPhrases, titlesCfg.rankSeparator,
                                            titlesCfg.standardizeHaulingNames);
        if (fresh && tags::routeEnabled(&titlesCfg) && isRouteTitle(titleKey) &&
            !titleHasRouteToken(baseTitle)) {
            const QString route = deriveRouteFragment(bodies(sortedDescs(puTitleToDescs.value(titleKey))),
                                                      &titlesCfg, loc, &routeCache);
            if (!route.isEmpty())
                current = tags::applyMissionTitle(current, route, titlesCfg);
        }
        std::vector<qint64> unique;
        if (ctx.showTitleTag(QStringLiteral("rep"))) {
            unique = xps;
            std::sort(unique.begin(), unique.end());
            unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
        }
        if (unique.size() == 1)
            current += QStringLiteral(" <EM4>[%1 %2]</EM4>").arg(xpText(unique.front()), repLabel);
        else if (unique.size() > 1)
            current += QStringLiteral(" <EM4>[%1–%2 %3]</EM4>")
                           .arg(xpText(unique.front()), xpText(unique.back()), repLabel);
        out.insert(titleKey, current);
    }

    if (ctx.rsOreNameAnnotations)
        for (const auto &[k, v] : mineableRsNameOverrides(loc))
            out.insert(k, v);
    return out;
}

} // namespace core::enh
