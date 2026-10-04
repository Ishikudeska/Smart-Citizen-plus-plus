#include "core/enhancements/Crafting.h"

#include "core/enhancements/Missions.h"
#include "core/text/PyText.h"

#include <QFile>
#include <QLoggingCategory>
#include <QSet>

#include <algorithm>
#include <map>
#include <set>

Q_DECLARE_LOGGING_CATEGORY(lcEnh)

namespace core::enh {

namespace {

const QSet<QString> &collectionItemKeys()
{
    // Collection-mission objectives (#97).
    // clang-format off
    static const QSet<QString> keys = {
        QStringLiteral("Mission_Item_0183"), QStringLiteral("Mission_Item_0184"), QStringLiteral("Mission_Item_0186"),
        QStringLiteral("Mission_Item_0191"), QStringLiteral("Mission_Item_0192"), QStringLiteral("Mission_Item_0195"),
        QStringLiteral("Mission_Item_0214"), QStringLiteral("harvestable_Armillaria"),
        QStringLiteral("items_commodities_amiantpod"), QStringLiteral("items_commodities_amioshiplague"),
        QStringLiteral("items_commodities_beradom"), QStringLiteral("items_commodities_carinite"),
        QStringLiteral("items_commodities_carinite_pure"), QStringLiteral("items_commodities_carinite_raw"),
        QStringLiteral("items_commodities_compboard"), QStringLiteral("items_commodities_decaripod"),
        QStringLiteral("items_commodities_degnousroot"), QStringLiteral("items_commodities_dopple"),
        QStringLiteral("items_commodities_feynmaline"), QStringLiteral("items_commodities_flareweedstalk"),
        QStringLiteral("items_commodities_fotiascrub"), QStringLiteral("items_commodities_freeze"),
        QStringLiteral("items_commodities_glacosite"), QStringLiteral("items_commodities_glow"),
        QStringLiteral("items_commodities_goldenmedmon"), QStringLiteral("items_commodities_heartofthewoods"),
        QStringLiteral("items_commodities_jaclium"), QStringLiteral("items_commodities_jaclium_ore"),
        QStringLiteral("items_commodities_janalite"), QStringLiteral("items_commodities_kopionhorn_irradiated"),
        QStringLiteral("items_commodities_mala"), QStringLiteral("items_commodities_marokgem"),
        QStringLiteral("items_commodities_pingala"), QStringLiteral("items_commodities_pitambu"),
        QStringLiteral("items_commodities_prota"), QStringLiteral("items_commodities_rantadung"),
        QStringLiteral("items_commodities_revenantpod"), QStringLiteral("items_commodities_sadaryx"),
        QStringLiteral("items_commodities_saldynium"), QStringLiteral("items_commodities_saldynium_ore"),
        QStringLiteral("items_commodities_stonebugshell"), QStringLiteral("items_commodities_sunsetberry"),
        QStringLiteral("items_commodities_valakkaregg_irradiated"),
        QStringLiteral("items_commodities_valakkarfang_adult"),
        QStringLiteral("items_commodities_valakkarfang_adult_irradiated"),
        QStringLiteral("items_commodities_valakkarfang_apex_irradiated"),
        QStringLiteral("items_commodities_valakkarfang_juvenile"),
        QStringLiteral("items_commodities_valakkarfang_juvenile_irradiated"),
        QStringLiteral("items_commodities_valakkarpearl_apex_irradiated"),
        QStringLiteral("items_commodities_valakkarpearl_apex_irradiated_tier1"),
        QStringLiteral("items_commodities_valakkarpearl_apex_irradiated_tier2"),
        QStringLiteral("items_commodities_valakkarpearl_apex_irradiated_tier3"),
        QStringLiteral("items_commodities_valakkarpearl_apex_irradiated_tier4"),
        QStringLiteral("items_commodities_valakkarpearl_apex_irradiated_tier5"),
        QStringLiteral("items_commodities_wuotanseed"), QStringLiteral("items_commodities_yormandi_eye"),
        QStringLiteral("items_commodities_zip"),
    };
    // clang-format on
    return keys;
}

bool pyLess(const QString &a, const QString &b)
{
    return py::less(a, b);
}

QStringList pySorted(QStringList list)
{
    std::sort(list.begin(), list.end(), pyLess);
    return list;
}

// sorted(..., key=str.lower), stable.
void sortCaseless(QStringList &list)
{
    std::stable_sort(list.begin(), list.end(),
                     [](const QString &a, const QString &b) { return py::less(a.toLower(), b.toLower()); });
}

// Windows Path ordering: lower-cased components, compared as lists.
bool pathLess(const QString &a, const QString &b)
{
    const QStringList pa = a.toLower().split(u'/');
    const QStringList pb = b.toLower().split(u'/');
    return std::lexicographical_compare(pa.begin(), pa.end(), pb.begin(), pb.end(), pyLess);
}

} // namespace

QHash<QString, QStringList> parseCompendiumLocations(const QString &content)
{
    QHash<QString, QStringList> result;
    if (content.isEmpty())
        return result;
    for (const auto paras = content.split(QStringLiteral("\\n\\n")); const QString &para : paras) {
        const qsizetype dash = para.indexOf(QStringLiteral(" - "));
        if (dash <= 0)
            continue;
        const QString name = py::strip(para.first(dash));
        if (py::len(name) > 40 || para.first(dash).contains(QStringLiteral("\\n")))
            continue;
        QStringList locs;
        for (const auto parts = para.sliced(dash + 3).split(u','); const QString &part : parts)
            if (const QString l = py::strip(part); !l.isEmpty())
                locs << l;
        sortCaseless(locs);
        if (!locs.isEmpty())
            result.insert(name.toLower(), locs);
    }
    return result;
}

const QStringList *lookupCommodityLocations(const QHash<QString, QStringList> &locations,
                                            const QString &display, const QString &internalName)
{
    QStringList candidates;
    const QString d = py::strip(display).toLower();
    if (!d.isEmpty()) {
        candidates << d;
        const QStringList words = d.split(py::re(QStringLiteral(R"(\s+)")), Qt::SkipEmptyParts);
        if (!words.isEmpty())
            candidates << words.front();
    }
    if (!internalName.isEmpty())
        candidates << internalName.toLower();
    for (const QString &k : std::as_const(candidates))
        if (const auto it = locations.constFind(k); it != locations.cend())
            return &*it;
    return nullptr;
}

namespace {

std::vector<std::pair<QString, QString>> discoverCommodityLocPairs(const QString &internalName,
                                                                   const Loc &loc)
{
    const QString prefix = QStringLiteral("items_commodities_") + internalName.toLower();
    QStringList nameKeys;
    QHash<QString, QString> descByBase;
    for (const auto &[key, value] : loc) {
        const QString low = key.toLower();
        if (!low.startsWith(prefix))
            continue;
        if (low.endsWith(QStringLiteral("_desc")))
            descByBase.insert(low.chopped(5), key);
        else if (low.endsWith(QStringLiteral("_des"))) {
            if (!descByBase.contains(low.chopped(4)))
                descByBase.insert(low.chopped(4), key);
        } else {
            nameKeys << key;
        }
    }
    std::vector<std::pair<QString, QString>> pairs;
    for (const QString &nameKey : std::as_const(nameKeys))
        if (const QString desc = descByBase.value(nameKey.toLower()); !desc.isEmpty())
            pairs.emplace_back(nameKey, desc);
    return pairs;
}

QString placeCommodityTag(const QString &base, const QString &tag, const tags::TagConfig &config)
{
    return tags::joinTag(base, tag, config.placement);
}

} // namespace

QString normalizeCommodityName(const QString &raw)
{
    QString n = raw.toLower();
    for (const char *prefix : {"ore_", "raw_", "processed_", "refined_"})
        if (n.startsWith(QLatin1StringView(prefix)))
            n = n.sliced(int(std::strlen(prefix)));
    for (const char *suffix : {"_ore", "_raw", "_processed", "_refined"})
        if (n.endsWith(QLatin1StringView(suffix)))
            n.chop(int(std::strlen(suffix)));
    return n;
}

QString humanizeCraftCategory(const QString &category)
{
    static const QHash<QString, QString> labels = {
        {QStringLiteral("powerplant"), QStringLiteral("Power Plants")},
        {QStringLiteral("cooler"), QStringLiteral("Coolers")},
        {QStringLiteral("radar"), QStringLiteral("Radars")},
        {QStringLiteral("shield"), QStringLiteral("Shields")},
        {QStringLiteral("quantumdrive"), QStringLiteral("Quantum Drives")},
        {QStringLiteral("jumpdrive"), QStringLiteral("Jump Drives")},
        {QStringLiteral("nozzle"), QStringLiteral("Refuel Nozzles")},
        {QStringLiteral("refuelling"), QStringLiteral("Refuel Nozzles")},
        {QStringLiteral("scanner"), QStringLiteral("Scanners")},
        {QStringLiteral("qed"), QStringLiteral("Quantum Enforcement Devices")},
    };
    QStringList parts;
    for (const auto segments = category.split(u'/'); const QString &p : segments)
        if (!p.isEmpty() && p != u"vehiclegear")
            parts << p;
    if (parts.isEmpty())
        return category;
    if (const QString l = labels.value(parts.back().toLower()); !l.isEmpty())
        return l;
    if (parts.size() >= 2)
        if (const QString l = labels.value(parts.mid(parts.size() - 2).join(u'/').toLower()); !l.isEmpty())
            return l;
    return py::title(QString(parts.back()).replace(u'_', u' '));
}

QString craftUsageKey(const QString &categoryPath)
{
    const QString p = categoryPath.toLower();
    if (p.contains(u"$templates"))
        return QString();
    const std::pair<const char *, const char *> rules[] = {
        {"quantumdrive", "Quantum Drive"},
        {"powerplant", "Power Plant"},
        {"cooler", "Cooler"},
        {"/shield", "Shield"},
        {"/radar", "Radar"},
        {"mininglaser", "Mining Laser"},
        {"tractorbeam", "Tractor Beam"},
        {"/salvage", "Salvage Module"},
    };
    for (const auto &[needle, key] : rules)
        if (p.contains(QLatin1StringView(needle)))
            return QString::fromLatin1(key);
    if (p.contains(u"nozzle") || p.contains(u"refuelling"))
        return QStringLiteral("Refuel Nozzle");
    if (p.contains(u"weapons/ballistic"))
        return QStringLiteral("Ship Weapon (Ballistic)");
    if (p.contains(u"weapons/laser"))
        return QStringLiteral("Ship Weapon (Energy)");
    if (p.contains(u"weapons/distortion"))
        return QStringLiteral("Ship Weapon (Distortion)");
    if (p.contains(u"fpsgear/weapons"))
        return QStringLiteral("FPS Weapon");
    if (p.contains(u"ammo"))
        return QStringLiteral("Ammo");
    if (p.contains(u"armour") || p.contains(u"armor"))
        return QStringLiteral("Armor");
    if (p.contains(u"missionitems"))
        return QStringLiteral("Mission Item");
    return QString();
}

QString craftUsageLegend(const tags::TagConfig *config)
{
    if (!config || config->elements.isEmpty())
        return QString();
    const tags::ElementSpec *usage = nullptr;
    for (const tags::ElementSpec &e : config->elements)
        if (e.kind == u"usage" && e.enabled) {
            usage = &e;
            break;
        }
    if (!usage || tags::craftUsageCategories().empty())
        return QString();
    const QString style = usage->style.isEmpty() ? QStringLiteral("long") : usage->style;
    const int idx = style == u"short" ? 0 : style == u"med" ? 1 : 2;
    const tags::Mapping &defaults = tags::defaultKindMappings().value(QStringLiteral("usage"));
    OrderedMap<QStringList> groups;
    for (const tags::UsageCategory &c : tags::craftUsageCategories()) {
        const QString name = QString::fromUtf8(c.name);
        tags::Variants variants;
        if (const auto it = config->classMapping.constFind(name); it != config->classMapping.cend())
            variants = *it;
        else if (const auto it2 = defaults.constFind(name); it2 != defaults.cend())
            variants = *it2;
        else
            variants = {QString::fromUtf8(c.shortCode), QString::fromUtf8(c.medCode),
                        QString::fromUtf8(c.longCode)};
        groups[QString::fromUtf8(c.group)]
            << QStringLiteral("- %1 = %2").arg(variants[std::size_t(idx)], name);
    }
    QStringList parts = {QStringLiteral("<EM3>Crafting Tag Key</EM3>")};
    for (const char *group : {"Ship Components", "FPS Gear", "Other"}) {
        const QStringList *rows = groups.find(QString::fromLatin1(group));
        if (!rows || rows->isEmpty())
            continue;
        parts << QString() << QStringLiteral("<EM4>%1:</EM4>").arg(QString::fromLatin1(group));
        parts << *rows;
    }
    return parts.join(kNl);
}

QString qdSizeRange(std::vector<int> sizes)
{
    std::sort(sizes.begin(), sizes.end());
    sizes.erase(std::unique(sizes.begin(), sizes.end()), sizes.end());
    if (sizes.size() == 1)
        return QStringLiteral("S%1").arg(sizes.front());
    if (sizes.back() - sizes.front() + 1 == int(sizes.size()))
        return QStringLiteral("S%1-S%2").arg(sizes.front()).arg(sizes.back());
    QStringList parts;
    for (const int s : sizes)
        parts << QStringLiteral("S%1").arg(s);
    return parts.join(QStringLiteral(", "));
}

QStringList condenseCraftedItems(const std::vector<std::pair<QString, QString>> &items)
{
    std::map<QString, QStringList, bool (*)(const QString &, const QString &)> byCat(pyLess);
    for (const auto &[cat, name] : items)
        byCat[cat] << name;
    static const QRegularExpression quoted = py::re(QStringLiteral(R"re(\s*"[^"]*"\s*)re"));
    static const QRegularExpression spaces = py::re(QStringLiteral(R"(\s+)"));
    static const QRegularExpression armourSet =
        py::re(QStringLiteral(R"(^([\w-]+(?:\s[\w-]+)?)\s+(?:Arms|Core|Legs|Helmet|Backpack|Suit|Armor))"));
    static const QRegularExpression qdSize(QStringLiteral(R"(quantumdrive/size(\d+)$)"),
                                           QRegularExpression::CaseInsensitiveOption);

    QStringList lines;
    qsizetype qdCount = 0;
    std::vector<int> qdSizes;
    for (const auto &[cat, rawNames] : byCat) {
        QStringList names = rawNames;
        std::sort(names.begin(), names.end(), pyLess);
        names.erase(std::unique(names.begin(), names.end()), names.end());
        const QStringList parts = cat.split(u'/');
        if (cat.contains(u"ammo")) {
            lines << (parts.size() > 2 ? py::title(parts.back()) : QStringLiteral("Ammo")) +
                         QStringLiteral(" Ammo");
            continue;
        }
        if (cat.contains(u"weapons")) {
            QSet<QString> bases;
            for (const QString &n : names) {
                QString clean = py::strip(QString(n).replace(quoted, QStringLiteral(" ")));
                bases.insert(clean.replace(spaces, QStringLiteral(" ")));
            }
            if (bases.size() <= 3)
                lines << pySorted(QStringList(bases.cbegin(), bases.cend())).join(QStringLiteral(", "));
            else
                lines << QStringLiteral("%1s (%2 types)").arg(py::title(parts.back())).arg(bases.size());
            continue;
        }
        if (cat.contains(u"armour")) {
            const QString weight = parts.size() > 2 ? py::title(parts.back()) : QString();
            const QString armourType =
                parts.size() > 2 ? py::title(parts[parts.size() - 2]) : QStringLiteral("Armour");
            QSet<QString> sets;
            for (const QString &n : names) {
                if (const QRegularExpressionMatch m = armourSet.match(n); m.hasMatch())
                    sets.insert(m.captured(1));
                else
                    sets.insert(n.isEmpty() ? n : n.split(spaces, Qt::SkipEmptyParts).value(0));
            }
            const QString label =
                sets.size() <= 3
                    ? pySorted(QStringList(sets.cbegin(), sets.cend())).join(QStringLiteral(", "))
                    : QStringLiteral("%1 sets").arg(sets.size());
            if (!weight.isEmpty() && armourType != weight)
                lines << QStringLiteral("%1 (%2 %3)").arg(label, weight, armourType);
            else
                lines << QStringLiteral("%1 (%2)").arg(label, armourType);
            continue;
        }
        if (const QRegularExpressionMatch m = qdSize.match(cat); m.hasMatch()) {
            qdCount += names.size();
            qdSizes.push_back(m.captured(1).toInt());
            continue;
        }
        lines << QStringLiteral("%1: %2 items").arg(humanizeCraftCategory(cat)).arg(names.size());
    }
    if (qdCount)
        lines << QStringLiteral("Quantum Drives: %1 items (%2)").arg(qdCount).arg(qdSizeRange(qdSizes));
    sortCaseless(lines);
    return lines;
}

QString commodityTag(const tags::TagConfig *config, bool crafting, bool collection,
                     const QStringList &usageKeys)
{
    QHash<QString, QString> values;
    if (crafting)
        values.insert(QStringLiteral("label"), QStringLiteral("Crafting"));
    if (!usageKeys.isEmpty()) {
        QStringList shown = usageKeys;
        constexpr int kMaxCodes = 4; // longer tags overflow the Fabricator (#208)
        if (shown.size() > kMaxCodes) {
            const qsizetype overflow = shown.size() - kMaxCodes;
            shown = shown.first(kMaxCodes);
            shown << QStringLiteral("+%1").arg(overflow);
        }
        values.insert(QStringLiteral("usage"), shown.join(tags::kUsageInputSep));
    }
    if (collection)
        values.insert(QStringLiteral("collection"), QStringLiteral("Collection"));
    if (values.isEmpty())
        return QString();
    const QString tag =
        tags::renderTag(config ? *config : defaultTagConfig(QStringLiteral("commodities")), values);
    return tag.isEmpty() ? QString() : QStringLiteral("<EM4>%1</EM4>").arg(tag);
}

std::pair<Loc, Loc> generateCommodityJournal(const Context &ctx)
{
    const RecordStore &store = *ctx.store;
    const Loc &loc = *ctx.loc;
    const QString bpDir = QStringLiteral("crafting/blueprints/crafting");
    const QString carryables = QStringLiteral("entities/scitem/carryables");
    const QString em = ctx.missionHeaderEm;
    const QString header = ctx.missionHeader(QStringLiteral("blueprint_data"));
    const tags::TagConfig &cfg = ctx.config(QStringLiteral("commodities"));
    if (!store.dirExists(bpDir))
        return {};
    const QString nullUuid = qs(kNullUuid);

    // Material UUIDs: CraftingCost_Resource@resource and CraftingCost_Item@entityClass.
    const auto materialUuid = [&](Node el) -> QString {
        const std::string_view pt = polyType(el);
        QString uid;
        if (pt == "CraftingCost_Resource")
            uid = qs(getOr(el, "resource"));
        else if (pt == "CraftingCost_Item")
            uid = qs(getOr(el, "entityClass"));
        return uid;
    };
    QSet<QString> uuids;
    for (const auto files = store.indexRglob(bpDir); const QString &file : files) {
        const XmlDoc doc = XmlDoc::load(file);
        if (!doc)
            continue;
        forEachElement(doc.root(), [&](Node el) {
            const QString uid = materialUuid(el);
            if (!uid.isEmpty() && uid != nullUuid)
                uuids.insert(uid);
            return true;
        });
    }

    // UUID -> commodity, by finding the UUID in carryable records.
    QHash<QString, QString> uuidNames;
    if (store.dirExists(carryables) && !uuids.isEmpty()) {
        static const QRegularExpression stemRe = py::re(QStringLiteral(
            R"((?:commodity_(?:metal|mineral|minerals|nonmetal|gas)|harvestable_(?:mineral|metal|ore)_\dh)_(\w+?)(?:_[a-d])?$)"));
        std::vector<QByteArray> needles;
        for (const QString &u : uuids)
            needles.push_back(u.toUtf8());
        for (const auto files = store.indexRglob(carryables); const QString &file : files) {
            QFile f(file);
            if (!f.open(QIODevice::ReadOnly))
                continue;
            const QByteArray content = f.readAll();
            QStringList matched;
            for (const QByteArray &n : needles)
                if (content.contains(n))
                    matched << QString::fromUtf8(n);
            if (matched.isEmpty())
                continue;
            if (const QRegularExpressionMatch m = stemRe.match(fileStem(file)); m.hasMatch()) {
                const QString commodity = normalizeCommodityName(m.captured(1));
                for (const QString &u : std::as_const(matched))
                    uuidNames.insert(u, commodity);
            }
        }
    }

    // Commodity -> (category, crafted item) for every blueprint.
    OrderedMap<std::vector<std::pair<QString, QString>>> commodityItems;
    QStringList bpFiles = store.indexRglob(bpDir);
    std::stable_sort(bpFiles.begin(), bpFiles.end(), pathLess);
    const QString bpRoot = store.absolute(bpDir) + u'/';
    for (const QString &file : std::as_const(bpFiles)) {
        const XmlDoc doc = XmlDoc::load(file);
        if (!doc)
            continue;
        const QString rel = file.mid(bpRoot.size());
        const qsizetype slash = rel.lastIndexOf(u'/');
        const QString category = slash < 0 ? QStringLiteral(".") : rel.first(slash);
        QString itemName = fileStem(file).replace(QStringLiteral("bp_craft_"), QString());
        forEachElement(doc.root(), [&](Node el) {
            if (polyType(el) != "CraftingProcess_Creation")
                return true;
            if (const auto it = ctx.scitem.entityNames.constFind(qs(getOr(el, "entityClass")));
                it != ctx.scitem.entityNames.cend())
                itemName = *it;
            return false;
        });
        std::set<QString, bool (*)(const QString &, const QString &)> materials(pyLess);
        forEachElement(doc.root(), [&](Node el) {
            const QString uid = materialUuid(el);
            if (!uid.isEmpty() || polyType(el) == "CraftingCost_Resource" ||
                polyType(el) == "CraftingCost_Item")
                if (const auto it = uuidNames.constFind(uid); it != uuidNames.cend())
                    materials.insert(*it);
            return true;
        });
        for (const QString &mat : materials)
            commodityItems[mat].emplace_back(category, itemName);
    }

    const QHash<QString, QStringList> mineralLocations =
        parseCompendiumLocations(loc.value(QStringLiteral("Journal_General_Mining_Compendium_Content")));

    Loc out;
    QStringList commodities;
    for (const auto &[commodity, items] : commodityItems)
        commodities << commodity;
    std::sort(commodities.begin(), commodities.end(), pyLess);
    QStringList skipped;
    for (const QString &commodity : std::as_const(commodities)) {
        const auto pairs = discoverCommodityLocPairs(commodity, loc);
        if (pairs.empty()) {
            skipped << commodity;
            continue;
        }
        const auto &items = *commodityItems.find(commodity);
        QStringList bullets;
        for (const auto lines = condenseCraftedItems(items); const QString &line : lines)
            bullets << QStringLiteral("- ") + line;
        const QString block = QStringLiteral("<%1>%2</%1>").arg(em, header) + kNl + bullets.join(kNl);
        QSet<QString> usage;
        for (const auto &[cat, name] : items)
            if (const QString k = craftUsageKey(cat); !k.isEmpty())
                usage.insert(k);
        const QStringList usageKeys = pySorted(QStringList(usage.cbegin(), usage.cend()));

        for (const auto &[nameKey, descKey] : pairs) {
            const QString baseName = loc.value(nameKey);
            if (!baseName.isEmpty() && !out.contains(nameKey)) {
                const QString tag =
                    commodityTag(&cfg, true, collectionItemKeys().contains(nameKey), usageKeys);
                out.insert(nameKey, tag.isEmpty() ? baseName : placeCommodityTag(baseName, tag, cfg));
            }
            const QString baseDesc = loc.value(descKey);
            if (!baseDesc.isEmpty() && !out.contains(descKey)) {
                QStringList sections;
                if (const QStringList *locs =
                        lookupCommodityLocations(mineralLocations, baseName, commodity)) {
                    QStringList lines;
                    for (const QString &l : *locs)
                        lines << QStringLiteral("- ") + l;
                    sections << QStringLiteral("<%1>Locations:</%1>").arg(em) + kNl + lines.join(kNl);
                }
                sections << block;
                out.insert(descKey,
                           baseDesc + QStringLiteral("\\n\\n") + sections.join(QStringLiteral("\\n\\n")));
            }
        }
    }
    if (!skipped.isEmpty())
        qCWarning(lcEnh) << "Crafting:" << skipped.size() << "commodities had no matching loc keys";

    // Collection objectives that aren't crafting materials.
    for (const QString &nameKey : collectionItemKeys()) {
        if (out.contains(nameKey))
            continue;
        const QString baseName = loc.value(nameKey);
        if (baseName.isEmpty())
            continue;
        if (const QString tag = commodityTag(&cfg, false, true); !tag.isEmpty())
            out.insert(nameKey, placeCommodityTag(baseName, tag, cfg));
    }

    // The Mining Compendium, restructured per mineral.
    Loc journal;
    const QString titleKey = QStringLiteral("Journal_General_Mining_Compendium_Title");
    const QString contentKey = QStringLiteral("Journal_General_Mining_Compendium_Content");
    const QString baseTitle = loc.value(titleKey);
    const QString baseContent = loc.value(contentKey);
    if (!baseTitle.isEmpty() && !baseContent.isEmpty()) {
        journal.insert(titleKey, baseTitle + QStringLiteral(" <EM4>[SmC]</EM4>"));
        QHash<QString, QStringList> mineralCrafting;
        for (const auto &[internal, items] : commodityItems) {
            const QStringList condensed = condenseCraftedItems(items);
            if (condensed.isEmpty())
                continue;
            QSet<QString> keys = {internal.toLower()};
            for (const auto &[nameKey, descKey] : discoverCommodityLocPairs(internal, loc)) {
                const QString display = py::strip(loc.value(nameKey)).toLower();
                if (display.isEmpty())
                    continue;
                keys.insert(display);
                const QStringList words = display.split(py::re(QStringLiteral(R"(\s+)")), Qt::SkipEmptyParts);
                if (!words.isEmpty())
                    keys.insert(words.front());
            }
            for (const QString &k : keys)
                if (!mineralCrafting.contains(k))
                    mineralCrafting.insert(k, condensed); // first writer wins
        }

        static const QHash<QString, QString> oreAliases = {
            {QStringLiteral("savrilium"), QStringLiteral("savrillium")}};
        QStringList augmented;
        for (const auto paras = baseContent.split(QStringLiteral("\\n\\n")); const QString &para : paras) {
            const qsizetype dash = para.indexOf(QStringLiteral(" - "));
            const QString name = dash > 0 ? py::strip(para.first(dash)) : QString();
            const auto locs =
                name.isEmpty() ? mineralLocations.cend() : mineralLocations.constFind(name.toLower());
            if (locs == mineralLocations.cend()) {
                augmented << para;
                continue;
            }
            QStringList block = {QStringLiteral("<EM3>%1</EM3>").arg(name), QString()};
            const QString oreKey = oreAliases.value(name.toLower(), name.toLower());
            const std::vector<int> steps = rsValueSteps(oreKey);
            // The flat base value only (MINEABLE_RS_VALUES), not the steps.
            if (const QString rsTag = formatRsTag({oreKey}); !rsTag.isEmpty()) {
                const QString value = rsTag.mid(4, rsTag.size() - 5);
                block << QStringLiteral("Base Resource Signature: <EM4>%1</EM4>").arg(value) << QString();
            }
            block << QStringLiteral("<EM4>Locations:</EM4>");
            for (const QString &l : *locs)
                block << QStringLiteral("- ") + l;
            if (const auto craft = mineralCrafting.constFind(name.toLower());
                craft != mineralCrafting.cend() && !craft->isEmpty()) {
                block << QString() << QStringLiteral("<EM4>Used To Craft:</EM4>");
                for (const QString &item : *craft)
                    block << QStringLiteral("- ") + item;
            }
            augmented << block.join(kNl);
        }
        if (const QString legend = craftUsageLegend(&cfg); !legend.isEmpty())
            augmented.prepend(legend);
        journal.insert(contentKey, augmented.join(QStringLiteral("\\n\\n")));
    }
    return {out, journal};
}

} // namespace core::enh
