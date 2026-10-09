#include "core/missions/Catalog.h"

#include "core/enhancements/Common.h"
#include "core/enhancements/Lookups.h"
#include "core/enhancements/Missions.h"
#include "core/enhancements/RecordStore.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cstring>

namespace core::missions {

using enh::find;
using enh::findAll;
using enh::getOr;
using enh::Node;
using enh::qs;
using enh::XmlDoc;

namespace {

// ~mission(Token) and ~mission(Token|Modifier).
const QRegularExpression &tokenPattern()
{
    static const QRegularExpression re(QStringLiteral(R"(~mission\(([^)|]*)(?:\|([^)]*))?\))"));
    return re;
}

qint64 toAmount(std::string_view value)
{
    return qRound64(qs(value).toDouble());
}

// What a token stands for: localization references the game picks one of,
// a location search, the organizations it can be, or a random NPC name.
struct TokenValue
{
    QStringList texts;
    int placeSet = -1;
    QStringList organizations; // by name
    bool randomName = false;
};

struct Organization
{
    QHash<QString, QString> strings; // by tag name: "Name" -> "@key"
    QSet<QString> tags;              // with their ancestors
};

// What one mission's properties and string params define.
struct Tokens
{
    QHash<QString, TokenValue> values;   // lower-cased token -> value
    std::vector<LocationSlot> locations; // in definition order
    QString missionType;                 // a contract's missionTypeOverride
};

// CIG pads some texts with runs of blank lines; keep one between paragraphs.
QString tidy(QString text)
{
    static const QRegularExpression blankRuns(QStringLiteral(R"(\n[ \t]*(?:\n[ \t]*){2,})"));
    return text.replace(blankRuns, QStringLiteral("\n\n")).trimmed();
}

// Whether `text` says anything beside unfilled [Tokens].
bool hasText(const QString &text)
{
    static const QRegularExpression tokens(QStringLiteral(R"(\[[^\]]*\])"));
    const QString rest = QString(text).remove(tokens);
    return std::any_of(rest.cbegin(), rest.cend(), [](QChar c) { return c.isLetterOrNumber(); });
}

QString slotKey(const LocationSlot &slot)
{
    return slot.variable.isEmpty() ? slot.token : slot.variable;
}

// "delivery" -> "Delivery", "pu_mercenary" -> "Pu mercenary".
QString folderLabel(const QString &folder)
{
    QString label = enh::humanizeKey(folder);
    if (!label.isEmpty())
        label[0] = label[0].toUpper();
    return label;
}

class Builder
{
public:
    Builder(const CatalogSources &sources, const enh::RecordStore &store);

    void addBrokerEntry(const QString &file);
    void addContractGenerator(const QString &file);
    Catalog take();

private:
    int placeSet(const LocationSearch &search);
    void applyProperty(Tokens &t, Node prop);
    void applyParams(Tokens &t, Node params);
    QString render(const QString &text, const Tokens &t, int depth = 0) const;
    QString tokenText(const QString &name, const QString &modifier, const Tokens &t, int depth) const;
    int variants(const QString &text, const Tokens &t) const;
    QString typeName(const QString &ref) const;
    QString relative(const QString &file) const;
    void addReputation(Mission &m, Node amount, bool success);
    void addBlueprints(Mission &m, Node contract);
    QString rankName(const QString &standing) const;
    void finish(Mission &m, const Tokens &t, const QString &nameHints);

    const enh::RecordStore &store_;
    IniMap noLoc_;
    LocText loc_;
    TagTable tags_;
    PlaceIndex index_;
    QStringList systems_;                        // known system names, for name hints
    QHash<QString, int> placeSetByKey_;          // LocationSearch::key() -> placeSets index
    QHash<QString, QString> typeNames_;          // MissionType __ref -> display name
    QHash<QString, XmlDoc> templates_;           // contract template __ref -> record
    QHash<QString, Organization> organizations_; // MissionOrganization __ref ->
    QStringList organizationOrder_;              // their refs, by name
    QHash<QString, qint64> reputation_;          // reputation reward __ref -> amount
    QHash<QString, QString> factions_;           // FactionReputation __ref -> name
    QHash<QString, QString> scopes_;             // SReputationScopeParams __ref -> track name
    enh::Standings standings_;
    enh::BlueprintPools pools_;
    Catalog catalog_;
};

Builder::Builder(const CatalogSources &sources, const enh::RecordStore &store)
    : store_(store), loc_(sources.loc ? *sources.loc : noLoc_), tags_(TagTable::load(sources.tagTable)),
      index_(PlaceIndex::build(store, tags_, loc_))
{
    catalog_.places = index_.places();
    catalog_.hasPlaces = !index_.isEmpty();
    systems_ = index_.systems();
    if (systems_.isEmpty())
        systems_ = {QStringLiteral("Nyx"), QStringLiteral("Pyro"), QStringLiteral("Stanton")};

    if (store.dirExists(QStringLiteral("missiontype")))
        for (const auto files = store.rglob(QStringLiteral("missiontype")); const QString &file : files) {
            const XmlDoc doc = XmlDoc::load(file);
            if (!doc || getOr(doc.root(), "__type") != "MissionType")
                continue;
            QString name = loc_(qs(getOr(doc.root(), "LocalisedTypeName")));
            if (name.isEmpty())
                name = enh::humanizeKey(enh::lastDotPart(enh::tag(doc.root())));
            typeNames_.insert(qs(getOr(doc.root(), "__ref")), name);
        }
    if (store.dirExists(QStringLiteral("missiondata/pu_organizations")))
        for (const auto files = store.rglob(QStringLiteral("missiondata/pu_organizations"));
             const QString &file : files) {
            const XmlDoc doc = XmlDoc::load(file);
            if (!doc || getOr(doc.root(), "__type") != "MissionOrganization")
                continue;
            Organization &org = organizations_[qs(getOr(doc.root(), "__ref"))];
            for (const Node variant : findAll(doc.root(), "stringVariants/variants/MissionStringVariant"))
                if (const QString kind = tags_.name(qs(getOr(variant, "tag")));
                    !kind.isEmpty() && !org.strings.contains(kind))
                    org.strings.insert(kind, qs(getOr(variant, "string")));
            for (const Node ref : findAll(doc.root(), ".//organizationTags//Reference"))
                for (const auto lineage = tags_.lineage(qs(getOr(ref, "value")));
                     const QString &tag : lineage)
                    org.tags.insert(tag);
        }
    organizationOrder_ = organizations_.keys();
    std::sort(organizationOrder_.begin(), organizationOrder_.end(),
              [this](const QString &a, const QString &b) {
                  const QString na = loc_(organizations_[a].strings.value(QStringLiteral("Name")));
                  const QString nb = loc_(organizations_[b].strings.value(QStringLiteral("Name")));
                  return na != nb ? na < nb : a < b;
              });
    if (store.dirExists(QStringLiteral("contracts/contracttemplates")))
        for (const auto files = store.rglob(QStringLiteral("contracts/contracttemplates"));
             const QString &file : files)
            if (XmlDoc doc = XmlDoc::load(file))
                templates_.insert(qs(getOr(doc.root(), "__ref")), std::move(doc));

    // Reputation: amounts by reward, and the names of factions, tracks and ranks.
    reputation_ = enh::buildReputationLookup(store);
    standings_ = enh::buildStandings(store, sources.loc ? *sources.loc : noLoc_);
    const auto names = [&](const char *dir, std::string_view type, const char *nameAttr,
                           const char *fallbackAttr, QHash<QString, QString> &into) {
        if (!store.dirExists(QLatin1StringView(dir)))
            return;
        for (const auto files = store.rglob(QLatin1StringView(dir)); const QString &file : files) {
            const XmlDoc doc = XmlDoc::load(file);
            if (!doc || getOr(doc.root(), "__type") != type)
                continue;
            QString name = loc_(qs(getOr(doc.root(), nameAttr)));
            if (name.isEmpty())
                name = enh::humanizeKey(qs(getOr(doc.root(), fallbackAttr)));
            into.insert(qs(getOr(doc.root(), "__ref")), name);
        }
    };
    names("factions/factionreputation", "FactionReputation", "displayName", "__ref", factions_);
    for (auto it = factions_.begin(); it != factions_.end(); ++it)
        if (it.value() == enh::humanizeKey(it.key())) // no display name: no use showing a GUID
            it.value().clear();
    names("reputation/scopes", "SReputationScopeParams", "displayName", "scopeName", scopes_);

    // Blueprint pools, with each blueprint's item named from the item record
    // its file name matches ("bp_craft_<item>"). The generator reads all
    // ~24k item records for those names; the blueprints need ~1.6k of them.
    QHash<QString, QString> itemFiles; // lower-cased file stem -> path
    if (store.dirExists(QStringLiteral("entities/scitem")))
        for (const auto files = store.rglob(QStringLiteral("entities/scitem")); const QString &file : files)
            itemFiles.insert(enh::fileStem(file).toLower(), file);
    QHash<QString, QString> itemNames; // lower-cased item stem -> display name
    if (store.dirExists(QStringLiteral("crafting/blueprints/crafting")))
        for (const auto files = store.rglob(QStringLiteral("crafting/blueprints/crafting"));
             const QString &file : files) {
            QString stem = enh::fileStem(file);
            for (const char *prefix : {"bp_craft_", "bp_rewards_", "bp_"})
                if (stem.startsWith(QLatin1StringView(prefix))) {
                    stem = stem.sliced(qsizetype(std::strlen(prefix)));
                    break;
                }
            stem = stem.toLower();
            const QString item = itemFiles.value(stem);
            if (item.isEmpty() || itemNames.contains(stem))
                continue;
            const XmlDoc doc = XmlDoc::load(item);
            QString name;
            enh::forEachElement(doc.root(), [&](Node el) {
                const std::string_view key = getOr(el, "Name");
                if (!key.starts_with('@'))
                    return true;
                name = loc_(qs(key));
                return false;
            });
            if (!name.isEmpty())
                itemNames.insert(stem, name);
        }
    pools_ = enh::buildBlueprintPoolLookup(store, {}, itemNames, {}, QString(), {});
}

void Builder::addReputation(Mission &m, Node amount, bool success)
{
    const auto it = reputation_.constFind(qs(getOr(amount, "reward")));
    if (it == reputation_.cend() || *it == 0)
        return;
    const ReputationReward reward{factions_.value(qs(getOr(amount, "factionReputation"))),
                                  scopes_.value(qs(getOr(amount, "reputationScope"))), *it, success};
    if (std::find(m.reputation.begin(), m.reputation.end(), reward) == m.reputation.end())
        m.reputation.push_back(reward);
}

// A contract's blueprint pools, merged where they share a rank and chance.
void Builder::addBlueprints(Mission &m, Node contract)
{
    for (const Node reward : enh::iter(contract, "BlueprintRewards")) {
        const QString pool = qs(getOr(reward, "blueprintPool"));
        const QStringList items = pools_.items.value(pool);
        if (items.isEmpty())
            continue;
        const QString label = enh::poolRankLabel(pools_.names.value(pool));
        const std::optional<double> chance = enh::toFloat(getOr(reward, "chance", "1"));
        BlueprintReward entry{label, chance ? std::clamp(*chance, 0.0, 1.0) : 1.0, items};
        const auto same =
            std::find_if(m.blueprints.begin(), m.blueprints.end(), [&entry](const BlueprintReward &b) {
                return b.label == entry.label && b.chance == entry.chance;
            });
        if (same == m.blueprints.end()) {
            m.blueprints.push_back(std::move(entry));
            continue;
        }
        for (const QString &item : items)
            if (!same->items.contains(item))
                same->items << item;
        same->items.sort(Qt::CaseInsensitive);
    }
}

// "Rank (Track)" for a standing, "" when unknown.
QString Builder::rankName(const QString &standing) const
{
    const QString rank = standings_.ranks.value(standing);
    if (rank.isEmpty())
        return {};
    const QString track = standings_.tracks.value(standing);
    return track.isEmpty() ? rank : QStringLiteral("%1 (%2)").arg(rank, track);
}

int Builder::placeSet(const LocationSearch &search)
{
    const QString key = search.key();
    if (const auto it = placeSetByKey_.constFind(key); it != placeSetByKey_.cend())
        return *it;
    const int id = static_cast<int>(catalog_.placeSets.size());
    catalog_.placeSets.push_back(index_.match(search));
    placeSetByKey_.insert(key, id);
    return id;
}

void Builder::applyProperty(Tokens &t, Node prop)
{
    const QString token = qs(getOr(prop, "extendedTextToken"));
    const QString variable = qs(getOr(prop, "missionVariableName"));
    const std::vector<Node> held = enh::children(find(prop, "value"));
    if (held.empty())
        return;
    const Node value = held.front();
    const std::string_view type = enh::tag(value);

    // A location, or (for a token) a combined value wrapping one.
    Node location;
    if (type == "MissionPropertyValue_Location")
        location = value;
    else if (!token.isEmpty() && type != "MissionPropertyValue_StringHash")
        location = find(value, ".//MissionPropertyValue_Location");
    if (location) {
        const LocationSearch search = parseLocationSearch(location);
        if (search.isEmpty())
            return;
        LocationSlot slot{variable, token, placeSet(search), {}};
        for (const auto guids = search.positiveTags(); const QString &guid : guids)
            if (const QString name = tags_.name(guid); !name.isEmpty())
                slot.searchTags << name;
        const QString key = slotKey(slot);
        if (key.isEmpty())
            return;
        const auto existing = std::find_if(t.locations.begin(), t.locations.end(),
                                           [&key](const LocationSlot &s) { return slotKey(s) == key; });
        if (existing == t.locations.end()) {
            t.locations.push_back(slot);
        } else {
            if (slot.token.isEmpty())
                slot.token = existing->token; // an override keeps the slot's token
            *existing = slot;
        }
        if (!slot.token.isEmpty())
            t.values[slot.token.toLower()].placeSet = slot.placeSet;
        return;
    }
    if (token.isEmpty())
        return;
    // Each kind of value is kept beside the others: a contract can give its
    // contractor as both a string and an organization, and either can lack
    // text.
    if (type == "MissionPropertyValue_StringHash") {
        QStringList texts;
        for (const Node option : findAll(value, "options/MissionPropertyValueOption_StringHash"))
            if (const QString id = qs(getOr(option, "textId")); !loc_(id).isEmpty())
                texts << id;
        if (!texts.isEmpty())
            t.values[token.toLower()].texts = texts;
    } else if (type == "MissionPropertyValue_AIName") {
        t.values[token.toLower()].randomName = true;
    } else if (type == "MissionPropertyValue_Organization") {
        // Named outright, or found by their tags.
        QSet<QString> named;
        for (const Node ref :
             findAll(value, ".//DataSetMatchCondition_SpecificOrganizationsDef/organizations/Reference"))
            named.insert(qs(getOr(ref, "value")));
        const LocationSearch search = parseLocationSearch(value);
        QStringList orgs;
        for (const QString &id : std::as_const(organizationOrder_))
            if ((named.isEmpty() || named.contains(id)) &&
                (search.isEmpty() || matchesGeneral(search, organizations_[id].tags)) &&
                (!named.isEmpty() || !search.isEmpty()))
                orgs << id;
        if (!orgs.isEmpty())
            t.values[token.toLower()].organizations = orgs;
    }
}

// A contract generator's contractParams or a contract's paramOverrides.
void Builder::applyParams(Tokens &t, Node params)
{
    if (!params)
        return;
    if (const std::string_view type = getOr(params, "missionTypeOverride");
        !type.empty() && type != enh::kNullUuid)
        t.missionType = qs(type);
    for (const Node param : findAll(params, "stringParamOverrides/ContractStringParam")) {
        const QString name = qs(getOr(param, "param"));
        const QString value = qs(getOr(param, "value"));
        if (!name.isEmpty() && !loc_(value).isEmpty())
            t.values[name.toLower()].texts = {value};
    }
    for (const Node prop : findAll(params, ".//MissionProperty"))
        applyProperty(t, prop);
}

QString Builder::render(const QString &text, const Tokens &t, int depth) const
{
    static const QRegularExpression markup(QStringLiteral(R"(</?EM\d*>)"));
    QString clean = text;
    clean.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
    clean.remove(markup);
    if (depth > 4) // tokens that refer to each other
        return clean;
    QString out;
    qsizetype last = 0;
    for (auto it = tokenPattern().globalMatch(clean); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        out += QStringView(clean).sliced(last, m.capturedStart() - last);
        out += tokenText(m.captured(1), m.captured(2), t, depth);
        last = m.capturedEnd();
    }
    out += QStringView(clean).sliced(last);
    return out;
}

QString Builder::tokenText(const QString &name, const QString &modifier, const Tokens &t, int depth) const
{
    const auto it = t.values.constFind(name.toLower());
    if (it == t.values.cend())
        return u'[' + name + u']';
    if (!it->organizations.isEmpty()) {
        const auto string = [this](const QString &id, const QString &kind) {
            return loc_(organizations_.value(id).strings.value(kind));
        };
        const QString nameKind = QStringLiteral("Name");
        // Who it can be: every name, when the game picks among several.
        if ((modifier.isEmpty() || modifier.compare(nameKind, Qt::CaseInsensitive) == 0) &&
            it->organizations.size() > 1) {
            QStringList names;
            for (const QString &id : it->organizations)
                if (const QString n = string(id, nameKind); !n.isEmpty() && !names.contains(n))
                    names << n;
            if (names.size() > 4)
                names = names.mid(0, 3) << QStringLiteral("+%1").arg(names.size() - 3);
            if (!names.isEmpty())
                return names.join(QStringLiteral(" / "));
        }
        // Otherwise the first one's string the modifier names, else its name.
        for (const QString &kind : {modifier.isEmpty() ? nameKind : modifier, nameKind})
            for (const QString &id : it->organizations)
                if (const QString text = string(id, kind); !text.isEmpty())
                    return render(text, t, depth + 1);
    }
    if (!it->texts.isEmpty())
        if (const QString text = loc_(it->texts.first()); !text.isEmpty())
            return render(text, t, depth + 1);
    if (it->placeSet >= 0) {
        const std::vector<int> &set = catalog_.placeSets[std::size_t(it->placeSet)];
        if (set.size() == 1) {
            const Place &p = catalog_.places[std::size_t(set.front())];
            return modifier.compare(u"Address", Qt::CaseInsensitive) == 0 && !p.address.isEmpty() ? p.address
                                                                                                  : p.name;
        }
    }
    return u'[' + name + u']';
}

// How many texts the game picks from when `text` is one token.
int Builder::variants(const QString &text, const Tokens &t) const
{
    const QRegularExpressionMatch m = tokenPattern().match(text.trimmed());
    if (!m.hasMatch() || m.capturedStart() != 0 || m.capturedLength() != text.trimmed().size())
        return 1;
    const auto it = t.values.constFind(m.captured(1).toLower());
    if (it == t.values.cend())
        return 1;
    if (!it->organizations.isEmpty())
        return int(it->organizations.size());
    return std::max<int>(1, int(it->texts.size()));
}

QString Builder::typeName(const QString &ref) const
{
    return typeNames_.value(ref);
}

QString Builder::relative(const QString &file) const
{
    return file.mid(store_.recordsDir().size()).section(u'/', 0, -1, QString::SectionSkipEmpty);
}

void Builder::finish(Mission &m, const Tokens &t, const QString &nameHints)
{
    // Slots with one search are one place to go, unless they name different
    // tokens (pickup and drop-off can share a search).
    for (const LocationSlot &slot : t.locations) {
        const auto same =
            std::find_if(m.locations.begin(), m.locations.end(), [&slot](const LocationSlot &s) {
                return s.placeSet == slot.placeSet &&
                       (s.token.isEmpty() || slot.token.isEmpty() || s.token == slot.token);
            });
        if (same == m.locations.end()) {
            m.locations.push_back(slot);
        } else if (same->token.isEmpty() && !slot.token.isEmpty()) {
            same->token = slot.token;
            same->variable = slot.variable;
        }
    }
    QSet<QString> systems;
    for (const LocationSlot &slot : m.locations)
        for (const int i : catalog_.placeSets[std::size_t(slot.placeSet)])
            if (const QString &s = catalog_.places[std::size_t(i)].system; !s.isEmpty())
                systems.insert(s);
    if (systems.isEmpty())
        for (const auto parts = nameHints.split(u'_', Qt::SkipEmptyParts); const QString &part : parts)
            for (const QString &s : std::as_const(systems_))
                if (part.compare(s, Qt::CaseInsensitive) == 0)
                    systems.insert(s);
    m.systems = QStringList(systems.cbegin(), systems.cend());
    m.systems.sort();
    catalog_.missions.push_back(std::move(m));
}

void Builder::addBrokerEntry(const QString &file)
{
    const XmlDoc doc = XmlDoc::load(file);
    if (!doc)
        return;
    const Node root = doc.root();
    if (getOr(root, "__type") != "MissionBrokerEntry" || getOr(root, "notForRelease") == "1")
        return;
    Tokens t;
    for (const Node prop : findAll(root, ".//MissionProperty"))
        applyProperty(t, prop);

    Mission m;
    m.id = enh::lastDotPart(enh::tag(root));
    m.file = relative(file);
    const QString title = loc_(qs(getOr(root, "title")));
    const QString description = loc_(qs(getOr(root, "description")));
    m.title = render(title, t).trimmed();
    if (!hasText(m.title))
        m.title = enh::humanizeKey(m.id);
    m.titleVariants = variants(title, t);
    m.description = tidy(render(description, t));
    m.descriptionVariants = variants(description, t);
    m.giver = render(loc_(qs(getOr(root, "missionGiver"))), t).trimmed();
    m.category = typeName(qs(getOr(root, "type")));
    if (m.category.isEmpty()) {
        const QStringList parts = m.file.split(u'/');
        const qsizetype i = parts.indexOf(QStringLiteral("pu_missions"));
        if (i >= 0 && i + 2 < parts.size())
            m.category = folderLabel(parts[i + 1]);
    }
    if (const Node reward = find(root, "missionReward")) {
        m.payout.amount = toAmount(getOr(reward, "reward"));
        m.payout.max = toAmount(getOr(reward, "max"));
        m.payout.currency = qs(getOr(reward, "currencyType"));
        if (m.payout.amount > 0 || m.payout.max > 0)
            m.payout.kind = Payout::Kind::Fixed;
    }
    m.payout.buyIn = toAmount(getOr(root, "missionBuyInAmount"));
    m.difficulty = enh::extractDifficulty(root);
    // The first outcome's reputation is for success; the others (failure,
    // abandoning) are mostly losses.
    const std::vector<Node> outcomes =
        findAll(root, ".//missionResultReputationRewards/SReputationAmountListParams");
    for (std::size_t i = 0; i < outcomes.size(); ++i)
        for (const Node amount : findAll(outcomes[i], ".//SReputationAmountParams"))
            addReputation(m, amount, i == 0);
    if (const Node required =
            find(find(root, ".//reputationRequirements"), ".//SReputationMissionGiverRequirementParams");
        required && getOr(required, "comparison") == "GreaterThanOrEqualTo")
        m.requiredRank = rankName(qs(getOr(required, "standing")));
    finish(m, t, m.id);
}

void Builder::addContractGenerator(const QString &file)
{
    const XmlDoc doc = XmlDoc::load(file);
    if (!doc)
        return;
    const QStringList parts = relative(file).split(u'/');
    const qsizetype at = parts.indexOf(QStringLiteral("contractgenerator"));
    const QString folder = at >= 0 && at + 2 < parts.size() ? folderLabel(parts[at + 1]) : QString();

    for (const auto &[handlerPath, contractPath] :
         {std::pair{".//ContractGeneratorHandler_Career", ".//CareerContract"},
          {".//ContractGeneratorHandler_List", ".//Contract"}}) {
        for (const Node handler : findAll(doc.root(), handlerPath)) {
            if (getOr(handler, "notForRelease") == "1")
                continue;
            const Node handlerParams = find(handler, "contractParams");
            const QString handlerName = qs(getOr(handler, "debugName"));
            for (const Node contract : findAll(handler, contractPath)) {
                if (getOr(contract, "notForRelease") == "1")
                    continue;
                // Template, then the generator's params, then the contract's own.
                Tokens t;
                QString templateTitle, templateDesc, templateType;
                if (const auto it = templates_.constFind(qs(getOr(contract, "template")));
                    it != templates_.cend()) {
                    const Node tmpl = it->root();
                    for (const Node prop : findAll(find(tmpl, "contractProperties"), ".//MissionProperty"))
                        applyProperty(t, prop);
                    templateType = qs(getOr(find(tmpl, ".//ContractDisplayInfo"), "type"));
                    for (const Node id : findAll(tmpl, ".//LocID")) {
                        const QString value = qs(getOr(id, "value"));
                        if (loc_(value).isEmpty())
                            continue;
                        if (value.contains(u"_title", Qt::CaseInsensitive) && templateTitle.isEmpty())
                            templateTitle = value;
                        else if (value.contains(u"_desc", Qt::CaseInsensitive) && templateDesc.isEmpty())
                            templateDesc = value;
                    }
                }
                applyParams(t, handlerParams);
                applyParams(t, find(contract, "paramOverrides"));

                const auto text = [&](const char *token, const QString &fallback) {
                    return t.values.contains(QLatin1StringView(token))
                               ? QStringLiteral("~mission(%1)").arg(QLatin1StringView(token))
                               : loc_(fallback);
                };
                const QString title = text("title", templateTitle);
                const QString description = text("description", templateDesc);
                Mission m;
                m.title = render(title, t).trimmed();
                if (m.title.isEmpty())
                    continue; // nothing the game could list
                if (!hasText(m.title))
                    m.title = enh::humanizeKey(qs(getOr(contract, "debugName")));
                m.contract = true;
                m.id = qs(getOr(contract, "debugName"));
                if (m.id.isEmpty())
                    m.id = handlerName;
                m.file = relative(file);
                m.titleVariants = variants(title, t);
                m.description = tidy(render(description, t));
                m.descriptionVariants = variants(description, t);
                m.giver = render(text("contractor", {}), t).trimmed();
                m.category = typeName(t.missionType);
                if (m.category.isEmpty())
                    m.category = typeName(templateType);
                if (m.category.isEmpty())
                    m.category = folder;
                if (find(contract, ".//ContractResult_CalculatedReward")) {
                    m.payout.kind = Payout::Kind::Calculated;
                } else if (const Node reward = find(contract, ".//contractReward")) {
                    m.payout.amount = toAmount(getOr(reward, "reward"));
                    m.payout.max = toAmount(getOr(reward, "max"));
                    m.payout.currency = qs(getOr(reward, "currencyType"));
                    if (m.payout.amount > 0 || m.payout.max > 0)
                        m.payout.kind = Payout::Kind::Fixed;
                }
                m.payout.buyIn = toAmount(getOr(find(contract, "contractResults"), "contractBuyInAmount"));
                m.difficulty = enh::extractDifficulty(contract);
                // Reputation flagged for the first outcome is for success.
                for (const Node legacy : findAll(contract, ".//ContractResult_LegacyReputation")) {
                    const Node amount = find(legacy, "contractResultReputationAmounts");
                    if (!amount)
                        continue;
                    const std::vector<Node> flags = findAll(legacy, "missionResults/Bool");
                    const auto flagged = [&flags](std::size_t from) {
                        return std::any_of(flags.begin() + qsizetype(std::min(from, flags.size())),
                                           flags.end(), [](Node b) { return getOr(b, "value") == "1"; });
                    };
                    const bool onSuccess = !flags.empty() && getOr(flags.front(), "value") == "1";
                    if (onSuccess || flagged(1))
                        addReputation(m, amount, onSuccess);
                    else if (const auto it = reputation_.constFind(qs(getOr(amount, "reward")));
                             it != reputation_.cend()) // no outcome flagged: by its sign
                        addReputation(m, amount, *it > 0);
                }
                m.requiredRank = rankName(qs(getOr(contract, "minStanding")));
                addBlueprints(m, contract);
                finish(m, t, m.id + u'_' + handlerName);
            }
        }
    }
}

Catalog Builder::take()
{
    QSet<QString> categories, systems;
    for (const Mission &m : catalog_.missions) {
        if (!m.category.isEmpty())
            categories.insert(m.category);
        for (const QString &s : m.systems)
            systems.insert(s);
    }
    catalog_.categories = QStringList(categories.cbegin(), categories.cend());
    catalog_.categories.sort(Qt::CaseInsensitive);
    catalog_.systems = QStringList(systems.cbegin(), systems.cend());
    catalog_.systems.sort();
    return std::move(catalog_);
}

bool cancelled(const std::atomic<bool> *cancel)
{
    return cancel && cancel->load();
}

} // namespace

Catalog buildCatalog(const CatalogSources &sources, const std::atomic<bool> *cancel)
{
    const auto store = enh::RecordStore::scan(sources.recordsDir);
    Builder builder(sources, *store);
    // A record the readers trip over is left out, not the whole catalog.
    const auto each = [&](const QString &dir, void (Builder::*add)(const QString &)) {
        if (!store->dirExists(dir))
            return;
        for (const auto files = store->rglob(dir); const QString &file : files) {
            if (cancelled(cancel))
                return;
            try {
                (builder.*add)(file);
            } catch (const std::exception &) {
            }
        }
    };
    each(QStringLiteral("missionbroker/pu_missions"), &Builder::addBrokerEntry);
    each(QStringLiteral("contracts/contractgenerator"), &Builder::addContractGenerator);
    if (cancelled(cancel))
        return {};
    return builder.take();
}

std::vector<int> filterMissions(const Catalog &catalog, const Filter &filter)
{
    const QString needle = filter.search.trimmed();
    std::vector<char> setHit;
    if (!needle.isEmpty()) {
        std::vector<char> placeHit(catalog.places.size());
        for (std::size_t i = 0; i < catalog.places.size(); ++i)
            placeHit[i] = char(catalog.places[i].name.contains(needle, Qt::CaseInsensitive) ||
                               catalog.places[i].address.contains(needle, Qt::CaseInsensitive));
        setHit.resize(catalog.placeSets.size());
        for (std::size_t s = 0; s < catalog.placeSets.size(); ++s)
            setHit[s] = char(std::any_of(catalog.placeSets[s].cbegin(), catalog.placeSets[s].cend(),
                                         [&placeHit](int i) { return placeHit[std::size_t(i)] != 0; }));
    }

    std::vector<int> out;
    for (std::size_t i = 0; i < catalog.missions.size(); ++i) {
        const Mission &m = catalog.missions[i];
        if (!filter.category.isEmpty() && m.category != filter.category)
            continue;
        if (!filter.system.isEmpty() && !m.systems.contains(filter.system))
            continue;
        if ((filter.payout == Filter::PayoutKind::Fixed && m.payout.kind != Payout::Kind::Fixed) ||
            (filter.payout == Filter::PayoutKind::Calculated && m.payout.kind != Payout::Kind::Calculated))
            continue;
        if (filter.blueprintsOnly && m.blueprints.empty())
            continue;
        if (!needle.isEmpty()) {
            const auto has = [&needle](const QString &s) { return s.contains(needle, Qt::CaseInsensitive); };
            const bool inText = has(m.title) || has(m.giver) || has(m.category) || has(m.description) ||
                                std::any_of(m.blueprints.cbegin(), m.blueprints.cend(),
                                            [&has](const BlueprintReward &b) {
                                                return std::any_of(b.items.cbegin(), b.items.cend(), has);
                                            }) ||
                                std::any_of(m.reputation.cbegin(), m.reputation.cend(),
                                            [&has](const ReputationReward &r) { return has(r.faction); });
            const bool inPlaces =
                std::any_of(m.locations.cbegin(), m.locations.cend(), [&setHit](const LocationSlot &s) {
                    return s.placeSet >= 0 && setHit[std::size_t(s.placeSet)] != 0;
                });
            if (!inText && !inPlaces)
                continue;
        }
        out.push_back(static_cast<int>(i));
    }

    // Titles by their first word, past any leading quote or bracket.
    const auto sortTitle = [](const QString &title) {
        qsizetype i = 0;
        while (i < title.size() && !title[i].isLetterOrNumber())
            ++i;
        return QStringView(title).sliced(i);
    };
    const auto byTitle = [&catalog, &sortTitle](int a, int b) {
        const Mission &x = catalog.missions[std::size_t(a)];
        const Mission &y = catalog.missions[std::size_t(b)];
        if (const int c = sortTitle(x.title).compare(sortTitle(y.title), Qt::CaseInsensitive); c != 0)
            return c < 0;
        if (const int c = x.title.compare(y.title, Qt::CaseInsensitive); c != 0)
            return c < 0;
        return x.giver.compare(y.giver, Qt::CaseInsensitive) < 0;
    };
    // Fixed payouts by amount, then calculated ones, then none.
    const auto byPayout = [&catalog, &byTitle](bool highFirst) {
        return [&catalog, &byTitle, highFirst](int a, int b) {
            const Payout &x = catalog.missions[std::size_t(a)].payout;
            const Payout &y = catalog.missions[std::size_t(b)].payout;
            if (x.kind != y.kind)
                return int(x.kind == Payout::Kind::Fixed ? 0
                           : x.kind == Payout::Kind::Calculated
                               ? 1
                               : 2) < int(y.kind == Payout::Kind::Fixed        ? 0
                                          : y.kind == Payout::Kind::Calculated ? 1
                                                                               : 2);
            if (x.kind == Payout::Kind::Fixed && x.amount != y.amount)
                return highFirst ? x.amount > y.amount : x.amount < y.amount;
            return byTitle(a, b);
        };
    };
    switch (filter.sort) {
    case Filter::Sort::Title:
        std::stable_sort(out.begin(), out.end(), byTitle);
        break;
    case Filter::Sort::PayoutHigh:
        std::stable_sort(out.begin(), out.end(), byPayout(true));
        break;
    case Filter::Sort::PayoutLow:
        std::stable_sort(out.begin(), out.end(), byPayout(false));
        break;
    }
    return out;
}

} // namespace core::missions
