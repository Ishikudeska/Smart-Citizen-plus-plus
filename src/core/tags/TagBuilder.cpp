#include "core/tags/TagBuilder.h"

#include "core/text/PyJson.h"
#include "core/text/PyText.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>

namespace core::tags {

namespace {

constexpr Option kStylesClass[] = {{"short", "Short (M)"}, {"med", "Medium (MIL)"}, {"long", "Long (Military)"}};
constexpr Option kStylesSize[] = {{"n", "Number (2)"}, {"sn", "S-prefixed (S2)"}, {"size_n", "Verbose (Size 2)"}};
constexpr Option kStylesGrade[] = {{"letter", "Letter (A)"}, {"grade_letter", "Verbose (Grade A)"}};
constexpr Option kStylesOrdinance[] = {{"short", "Short (I)"}, {"med", "Medium (IR)"}, {"long", "Long (Infrared)"}};
constexpr Option kStylesDamage[] = {{"short", "Short (E)"}, {"med", "Medium (EN)"}, {"long", "Long (Energy)"}};
constexpr Option kStylesType[] = {{"short", "Short (SH)"}, {"med", "Medium (SHLD)"}, {"long", "Long (Shield)"}};
constexpr Option kStylesLabel[] = {{"short", "Short (CF)"}, {"med", "Medium (Craft)"}, {"long", "Long (Crafting)"}};
constexpr Option kStylesCollection[] = {
    {"short", "Short (Col)"}, {"med", "Medium (Collect)"}, {"long", "Long (Collection)"}};
constexpr Option kStylesUsage[] = {{"short", "Short (Q)"}, {"med", "Medium (QD)"}, {"long", "Long (QDRV)"}};

constexpr TextOption kSeparators[] = {
    {"none", "None", ""},           {"space", "Space", " "},
    {"hyphen", "Hyphen ( - )", "-"}, {"underscore", "Underscore ( _ )", "_"},
    {"dot", "Period ( . )", "."},   {"slash", "Slash ( / )", "/"},
    {"pipe", "Pipe ( | )", "|"},
};
constexpr Enclosing kEnclosings[] = {
    {"none", "None (space only)", "", ""}, {"square", "Square [ ]", "[", "]"}, {"round", "Round ( )", "(", ")"},
    {"curly", "Curly { }", "{", "}"},      {"angle", "Angle < >", "<", ">"},
};
// "->" not "→": mobiGlas has no glyph for U+2192 (#200). "shape" is built
// from the endpoint counts in renderRoute; its text here is the 1:1 form.
constexpr TextOption kRouteArrows[] = {
    {"gt", "Greater-than ( > )", ">"},
    {"arrow", "Arrow ( -> )", "->"},
    {"to", "Word ( to )", "to"},
    {"shape", "Route shape ( ->- / ->= / =>- / =>= )", "->-"},
};
constexpr TextOption kTitleSeparators[] = {
    {"dash", "Dash ( - )", " - "},
    {"pipe", "Pipe ( | )", " | "},
    {"colon", "Colon ( : )", ": "},
    {"space", "Space", " "},
};
constexpr Option kPlacements[] = {{"prepend", "Before name (default)"}, {"append", "After name"}};
constexpr Option kMissionTitlePlacements[] = {
    {"append", "After title"}, {"prepend", "Before title"}, {"replace", "Replace title"}};
constexpr Option kLocationDetails[] = {{"address", "Full address"}, {"name", "Name"}};

constexpr PhraseOption kShortenPhrases[] = {
    {"intro", "Shorten \"Opportunity for Independent Cargo Hauler\" to \"Intro\"",
     "Opportunity for Independent Cargo Hauler", "Intro"},
    // The "-" is a fallback; abbreviateTitle uses the live rank separator.
    {"hauler_needed_for", "Shorten \"Hauler Needed for\" to the Rank separator", "Hauler Needed for", "-"},
    {"local_shipment_route", "Shorten \"Local Shipment Route\" to \"Route\"", "Local Shipment Route", "Route"},
    // Ling Family titles put "Cargo" right after the rank token; the
    // replacement is rebuilt from the rank settings in abbreviateTitle.
    {"ling_family_rank", "Add the Rank separator to Ling Family haul titles", "~mission(ReputationRank) Cargo",
     "~mission(ReputationRank) Rank Cargo"},
    {"ling_family_prefix", "Shorten \"Ling Family\" by removing it", "Ling Family ", ""},
};
constexpr PhraseOption kRemoveWords[] = {
    {"cargo", "Remove \"Cargo\"", "Cargo", ""},
    {"haul", "Remove \"Haul\"", "Haul", ""},
    {"rank", "Remove \"Rank\"", "", ""}, // handled through the rank triggers
};
constexpr PhraseOption kUnderline[] = {
    {"underline_direct", "Underline \"Direct\"", "Direct", "<EM3>DIRECT</EM3>"},
};

// The punctuation after "Rank" in stock titles; the leading space keeps
// "~mission(ReputationRank)" out of it.
constexpr const char *kRankTriggers[] = {" Rank -", " Rank,"};

constexpr std::pair<const char *, const char *> kSizeAbbreviations[] = {
    {"Extra Small", "XS"}, {"Extra Large", "XL"}, {"Small", "S"}, {"Medium", "M"}, {"Large", "L"},
};

constexpr UsageCategory kUsageCategories[] = {
    {"Power Plant", "P", "PP", "POWR", "Ship Components"},
    {"Cooler", "C", "CL", "COOL", "Ship Components"},
    {"Shield", "S", "SH", "SHLD", "Ship Components"},
    {"Radar", "R", "RD", "RADR", "Ship Components"},
    {"Quantum Drive", "Q", "QD", "QDRV", "Ship Components"},
    {"Mining Laser", "M", "ML", "MINE", "Ship Components"},
    {"Tractor Beam", "T", "TB", "TRAC", "Ship Components"},
    {"Salvage Module", "SV", "SLV", "SALV", "Ship Components"},
    {"Refuel Nozzle", "F", "RF", "FUEL", "Ship Components"},
    {"Ship Weapon (Ballistic)", "B", "BW", "BGUN", "Ship Components"},
    {"Ship Weapon (Energy)", "E", "EW", "EGUN", "Ship Components"},
    {"Ship Weapon (Distortion)", "D", "DW", "DGUN", "Ship Components"},
    {"FPS Weapon", "G", "FW", "FPSW", "FPS Gear"},
    {"Ammo", "A", "AM", "AMMO", "FPS Gear"},
    {"Armor", "AR", "ARM", "ARMR", "FPS Gear"},
    {"Mission Item", "MI", "MIT", "MITM", "Other"},
};

QString s(const char *latin)
{
    return QString::fromUtf8(latin);
}

Mapping mapping(std::initializer_list<std::pair<const char *, std::array<const char *, 3>>> rows)
{
    Mapping m;
    for (const auto &[key, v] : rows)
        m.insert(s(key), {s(v[0]), s(v[1]), s(v[2])});
    return m;
}

template <class T>
const char *textFor(std::span<const T> table, const QString &key, const char *T::*field, const char *fallback)
{
    for (const T &row : table)
        if (key == QLatin1StringView(row.key))
            return row.*field;
    return fallback;
}

bool hasKey(std::span<const TextOption> table, const QString &key)
{
    return std::any_of(table.begin(), table.end(), [&key](const TextOption &o) { return key == QLatin1StringView(o.key); });
}

QSet<QString> legacyMigrationKeys()
{
    QSet<QString> keys;
    for (const auto &o : kShortenPhrases)
        keys.insert(s(o.key));
    for (const auto &o : kRemoveWords)
        keys.insert(s(o.key));
    return keys;
}

QSet<QString> abbreviationKeys()
{
    QSet<QString> keys = legacyMigrationKeys();
    for (const auto &o : kUnderline)
        keys.insert(s(o.key));
    return keys;
}

QSet<QString> sizeWords()
{
    QSet<QString> words;
    for (const auto &[word, abbrev] : kSizeAbbreviations)
        words.insert(s(word));
    return words;
}

QString styleValue(const QString &kind, const QString &style, const QString &raw, const Mapping &mapping)
{
    if (raw.isEmpty())
        return {};
    if (kind == u"size") {
        if (style == u"n")
            return raw;
        if (style == u"size_n")
            return QStringLiteral("Size ") + raw;
        return u'S' + raw; // "sn" and anything unknown
    }
    if (kind == u"grade")
        return style == u"grade_letter" ? QStringLiteral("Grade ") + raw : raw;
    if (isMappedKind(kind)) {
        const auto it = mapping.constFind(raw);
        if (it == mapping.cend())
            return raw; // shown as-is so the user can add it to the mapping
        const int index = style == u"short" ? 0 : style == u"long" ? 2 : 1;
        return (*it)[static_cast<std::size_t>(index)];
    }
    return raw;
}

// " ".join(text.split()): runs of Python whitespace become one space.
QString collapseWhitespace(const QString &text)
{
    QString out;
    bool pending = false;
    for (const QChar c : text) {
        if (py::isSpace(c)) {
            pending = !out.isEmpty();
            continue;
        }
        if (pending)
            out += u' ';
        pending = false;
        out += c;
    }
    return out;
}

QString replaceWord(const QString &text, const QString &word, const QString &replacement)
{
    const QRegularExpression re(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(word)),
                                QRegularExpression::UseUnicodePropertiesOption);
    QString out = text;
    out.replace(re, replacement);
    return out;
}

// Rewrites a procedural haul title (it has both the rank and size tokens)
// into one canonical "Rank <sep> [Direct] Size Cargo Haul [Circuit]".
QString standardizeHaulingTitle(const QString &title, bool removeRank, const QString &sep)
{
    static const QString rankToken = QStringLiteral("~mission(ReputationRank)");
    static const QString sizeToken = QStringLiteral("~mission(CargoGradeToken)");
    if (!title.contains(rankToken) || !title.contains(sizeToken))
        return title;
    static const QRegularExpression directRe(QStringLiteral("\\bDirect\\b"), QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression circuitRe(QStringLiteral("\\bCircuit\\b"), QRegularExpression::UseUnicodePropertiesOption);
    const QString direct = directRe.match(title).hasMatch() ? QStringLiteral("Direct ") : QString();
    const QString circuit = circuitRe.match(title).hasMatch() ? QStringLiteral(" Circuit") : QString();
    const QString word = removeRank ? QString() : QStringLiteral("Rank");
    return rankToken + u' ' + word + sep + u' ' + direct + sizeToken + QStringLiteral(" Cargo Haul") + circuit;
}

QString stringOf(const QJsonValue &v, const QString &fallback)
{
    if (v.isString() && !v.toString().isEmpty())
        return v.toString();
    return fallback;
}

QString jsonList(const QStringList &items)
{
    QStringList quoted;
    for (const QString &i : items)
        quoted << py::jsonString(i, false);
    return u'[' + quoted.join(QStringLiteral(", ")) + u']';
}

QStringList sortedList(const QSet<QString> &set)
{
    QStringList list(set.cbegin(), set.cend());
    std::sort(list.begin(), list.end());
    return list;
}

} // namespace

QStringList elementKinds(const QString &category)
{
    if (category == u"components")
        return {QStringLiteral("class"), QStringLiteral("size"), QStringLiteral("grade"), QStringLiteral("type")};
    if (category == u"missiles")
        return {QStringLiteral("ordinance"), QStringLiteral("size")};
    if (category == u"ship_weapons")
        return {QStringLiteral("damage"), QStringLiteral("size")};
    if (category == u"commodities")
        return {QStringLiteral("label"), QStringLiteral("usage"), QStringLiteral("collection")};
    if (category == u"mission_titles")
        return {QStringLiteral("route")};
    return {};
}

std::span<const Option> stylesFor(const QString &kind)
{
    if (kind == u"class") return kStylesClass;
    if (kind == u"size") return kStylesSize;
    if (kind == u"grade") return kStylesGrade;
    if (kind == u"ordinance") return kStylesOrdinance;
    if (kind == u"damage") return kStylesDamage;
    if (kind == u"type") return kStylesType;
    if (kind == u"label") return kStylesLabel;
    if (kind == u"collection") return kStylesCollection;
    if (kind == u"usage") return kStylesUsage;
    return {};
}

QString elementLabel(const QString &kind)
{
    static const QHash<QString, QString> labels = {
        {QStringLiteral("class"), QStringLiteral("Class")},       {QStringLiteral("size"), QStringLiteral("Size")},
        {QStringLiteral("grade"), QStringLiteral("Grade")},       {QStringLiteral("ordinance"), QStringLiteral("Ordinance")},
        {QStringLiteral("damage"), QStringLiteral("Damage type")}, {QStringLiteral("type"), QStringLiteral("Type")},
        {QStringLiteral("label"), QStringLiteral("Label")},       {QStringLiteral("usage"), QStringLiteral("Used To Craft")},
        {QStringLiteral("collection"), QStringLiteral("Collection")}, {QStringLiteral("route"), QStringLiteral("Route")},
    };
    return labels.value(kind, kind);
}

std::span<const TextOption> separators() { return kSeparators; }
std::span<const TextOption> routeArrows() { return kRouteArrows; }
std::span<const TextOption> titleSeparators() { return kTitleSeparators; }
std::span<const Enclosing> enclosings() { return kEnclosings; }
std::span<const Option> placements() { return kPlacements; }
std::span<const Option> missionTitlePlacements() { return kMissionTitlePlacements; }
std::span<const Option> locationDetails() { return kLocationDetails; }
std::span<const PhraseOption> shortenPhraseOptions() { return kShortenPhrases; }
std::span<const PhraseOption> removeWordOptions() { return kRemoveWords; }
std::span<const PhraseOption> underlineOptions() { return kUnderline; }
std::span<const std::pair<const char *, const char *>> sizeAbbreviations() { return kSizeAbbreviations; }
std::span<const UsageCategory> craftUsageCategories() { return kUsageCategories; }

QString damageMappingKey(const QString &compactLabel)
{
    if (compactLabel == u"Phys")
        return QStringLiteral("Physical");
    if (compactLabel == u"Distort")
        return QStringLiteral("Distortion");
    if (compactLabel == u"Bio")
        return QStringLiteral("Biochemical");
    return compactLabel;
}

const QHash<QString, Mapping> &defaultKindMappings()
{
    static const QHash<QString, Mapping> mappings = [] {
        QHash<QString, Mapping> m;
        m.insert(QStringLiteral("class"), mapping({{"Competition", {"R", "CMP", "Competition"}},
                                                    {"Military", {"M", "MIL", "Military"}},
                                                    {"Civilian", {"C", "CIV", "Civilian"}},
                                                    {"Industrial", {"I", "IND", "Industrial"}},
                                                    {"Stealth", {"S", "STH", "Stealth"}}}));
        m.insert(QStringLiteral("type"), mapping({{"Shield Generator", {"SH", "SHLD", "Shield"}},
                                                   {"Cooler", {"CL", "COOL", "Cooler"}},
                                                   {"Power Plant", {"PW", "POWR", "Power"}},
                                                   {"Quantum Drive", {"QD", "QDRV", "Quantum"}},
                                                   {"Radar", {"RD", "RADR", "Radar"}},
                                                   {"Fuel Nozzle", {"FN", "FNoz", "Fuel Nozzle"}},
                                                   {"Mining Laser", {"ML", "MineL", "Mining Laser"}}}));
        m.insert(QStringLiteral("ordinance"), mapping({{"Infrared", {"I", "IR", "Infrared"}},
                                                        {"Electromagnetic", {"E", "EM", "Electromagnetic"}},
                                                        {"CrossSection", {"C", "CS", "CrossSection"}},
                                                        {"Bomb", {"B", "B", "Bomb"}}}));
        m.insert(QStringLiteral("damage"), mapping({{"Energy", {"E", "EN", "Energy"}},
                                                     {"Physical", {"P", "PHY", "Physical"}},
                                                     {"Distortion", {"D", "DIS", "Distortion"}},
                                                     {"Thermal", {"T", "THM", "Thermal"}},
                                                     {"Biochemical", {"B", "BIO", "Biochemical"}},
                                                     {"Stun", {"S", "STN", "Stun"}}}));
        m.insert(QStringLiteral("label"), mapping({{"Crafting", {"CF", "Craft", "Crafting"}}}));
        m.insert(QStringLiteral("collection"), mapping({{"Collection", {"Col", "Collect", "Collection"}}}));
        Mapping usage;
        for (const UsageCategory &u : kUsageCategories)
            usage.insert(s(u.name), {s(u.shortCode), s(u.medCode), s(u.longCode)});
        m.insert(QStringLiteral("usage"), usage);
        return m;
    }();
    return mappings;
}

bool isMappedKind(const QString &kind)
{
    return defaultKindMappings().contains(kind);
}

TagConfig defaultConfig(const QString &category)
{
    const auto &maps = defaultKindMappings();
    auto merged = [&maps](std::initializer_list<const char *> kinds) {
        Mapping m;
        for (const char *k : kinds)
            m.insert(maps.value(s(k)));
        return m;
    };
    auto el = [](const char *kind, bool enabled, const char *style) { return ElementSpec{s(kind), enabled, s(style)}; };

    TagConfig c;
    if (category == u"components") {
        c.elements = {el("class", true, "med"), el("size", true, "sn"), el("grade", true, "letter"),
                      el("type", false, "short")};
        c.classMapping = merged({"class", "type"});
    } else if (category == u"missiles") {
        c.elements = {el("ordinance", true, "med"), el("size", true, "sn")};
        c.classMapping = merged({"ordinance"});
    } else if (category == u"ship_weapons") {
        c.elements = {el("damage", true, "short"), el("size", true, "sn")};
        c.classMapping = merged({"damage"});
    } else if (category == u"commodities") {
        // All off by default (#325): untagged names until the user opts in.
        c.elements = {el("label", false, "short"), el("usage", false, "long"), el("collection", false, "long")};
        c.separator = QStringLiteral("pipe");
        c.placement = QStringLiteral("append");
        c.classMapping = merged({"label", "collection", "usage"});
    } else if (category == u"mission_titles") {
        c.elements = {el("route", true, "")};
        c.placement = QStringLiteral("append");
    }
    return c;
}

QString TagConfig::toJson() const
{
    QStringList mappingItems;
    for (auto it = classMapping.cbegin(); it != classMapping.cend(); ++it)
        mappingItems << py::jsonString(it.key(), false) + QStringLiteral(": ") +
                            jsonList({it.value()[0], it.value()[1], it.value()[2]});
    QStringList elementItems;
    for (const ElementSpec &e : elements)
        elementItems << QStringLiteral("{\"enabled\": %1, \"kind\": %2, \"style\": %3}")
                            .arg(py::jsonBool(e.enabled), py::jsonString(e.kind, false), py::jsonString(e.style, false));

    auto field = [](const char *key, const QString &value) { return u'"' + s(key) + QStringLiteral("\": ") + value; };
    const QStringList fields = {
        field("abbreviated_phrases", jsonList(sortedList(abbreviatedPhrases))),
        field("class_mapping", u'{' + mappingItems.join(QStringLiteral(", ")) + u'}'),
        field("elements", u'[' + elementItems.join(QStringLiteral(", ")) + u']'),
        field("enclosing", py::jsonString(enclosing, false)),
        field("location_detail", py::jsonString(locationDetail, false)),
        field("placement", py::jsonString(placement, false)),
        field("rank_separator", py::jsonString(rankSeparator, false)),
        field("route_arrow", py::jsonString(routeArrow, false)),
        field("separator", py::jsonString(separator, false)),
        field("shortened_sizes", jsonList(sortedList(shortenedSizes))),
        field("standardize_hauling_names", py::jsonBool(standardizeHaulingNames)),
        field("title_separator", py::jsonString(titleSeparator, false)),
        field("usage_separator", py::jsonString(usageSeparator, false)),
    };
    return u'{' + fields.join(QStringLiteral(", ")) + u'}';
}

TagConfig TagConfig::fromObject(const QJsonObject &d)
{
    TagConfig c;
    for (const auto specs = d.value(QStringLiteral("elements")).toArray(); const QJsonValue &v : specs) {
        const QJsonObject e = v.toObject();
        const QString kind = e.value(QStringLiteral("kind")).toString();
        if (!v.isObject() || kind.isEmpty())
            continue;
        c.elements.append({kind, e.value(QStringLiteral("enabled")).toBool(true), e.value(QStringLiteral("style")).toString()});
    }

    const QJsonObject mappingRaw = d.value(QStringLiteral("class_mapping")).toObject();
    for (auto it = mappingRaw.begin(); it != mappingRaw.end(); ++it) {
        const QJsonArray v = it.value().toArray();
        auto str = [&v](qsizetype i) { return v.at(i).toVariant().toString(); };
        if (v.size() >= 3)
            c.classMapping.insert(it.key(), {str(0), str(1), str(2)});
        else if (v.size() == 2)
            c.classMapping.insert(it.key(), {str(0), str(0), str(1)});
    }

    c.placement = stringOf(d.value(QStringLiteral("placement")), QStringLiteral("prepend"));
    if (c.placement != u"prepend" && c.placement != u"append" && c.placement != u"replace")
        c.placement = QStringLiteral("prepend");

    const bool legacyOn = d.value(QStringLiteral("abbreviate_title")).toBool(false);
    const QJsonValue phrases = d.value(QStringLiteral("abbreviated_phrases"));
    if (phrases.isUndefined() || phrases.isNull()) {
        // Pre-redesign blobs stored one bool; opting in meant every option.
        if (legacyOn)
            c.abbreviatedPhrases = legacyMigrationKeys();
    } else {
        const QSet<QString> valid = abbreviationKeys();
        for (const auto phraseList = phrases.toArray(); const QJsonValue &k : phraseList)
            if (valid.contains(k.toString()))
                c.abbreviatedPhrases.insert(k.toString());
    }

    c.rankSeparator = stringOf(d.value(QStringLiteral("rank_separator")), QStringLiteral("dash"));
    if (!hasKey(kTitleSeparators, c.rankSeparator))
        c.rankSeparator = QStringLiteral("dash");

    const QJsonValue sizes = d.value(QStringLiteral("shortened_sizes"));
    if (sizes.isUndefined() || sizes.isNull()) {
        if (legacyOn)
            c.shortenedSizes = sizeWords();
    } else {
        const QSet<QString> words = sizeWords();
        for (const auto sizeList = sizes.toArray(); const QJsonValue &w : sizeList)
            if (words.contains(w.toString()))
                c.shortenedSizes.insert(w.toString());
    }

    c.separator = stringOf(d.value(QStringLiteral("separator")), QStringLiteral("hyphen"));
    c.enclosing = stringOf(d.value(QStringLiteral("enclosing")), QStringLiteral("square"));
    c.usageSeparator = stringOf(d.value(QStringLiteral("usage_separator")), QStringLiteral("pipe"));
    c.routeArrow = stringOf(d.value(QStringLiteral("route_arrow")), QStringLiteral("gt"));
    c.titleSeparator = stringOf(d.value(QStringLiteral("title_separator")), QStringLiteral("dash"));
    c.locationDetail = stringOf(d.value(QStringLiteral("location_detail")), QStringLiteral("address"));
    c.standardizeHaulingNames = d.value(QStringLiteral("standardize_hauling_names")).toBool(false);
    return c;
}

std::optional<TagConfig> TagConfig::fromJson(const QString &json)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (!doc.isObject())
        return std::nullopt;
    return fromObject(doc.object());
}

void migrateMapping(const QString &category, TagConfig &config)
{
    if (category != u"ship_weapons")
        return;
    for (const char *old : {"Phys", "Distort", "Bio"}) {
        const QString oldKey = s(old);
        if (!config.classMapping.contains(oldKey))
            continue;
        const QString newKey = damageMappingKey(oldKey);
        const Variants value = config.classMapping.take(oldKey);
        if (!config.classMapping.contains(newKey))
            config.classMapping.insert(newKey, value);
    }
}

void backfillNewElements(const QString &category, TagConfig &config)
{
    const QStringList expected = elementKinds(category);
    QSet<QString> existing;
    for (const ElementSpec &e : std::as_const(config.elements))
        existing.insert(e.kind);
    const TagConfig defaults = defaultConfig(category);

    for (qsizetype pos = 0; pos < expected.size(); ++pos) {
        const QString &kind = expected[pos];
        if (!existing.contains(kind)) {
            ElementSpec spec{kind, false, QString()};
            for (const ElementSpec &d : defaults.elements)
                if (d.kind == kind)
                    spec = d;
            // After the last present element that precedes it canonically.
            const QStringList preceding = expected.mid(0, pos);
            qsizetype insertAt = 0;
            for (qsizetype i = 0; i < config.elements.size(); ++i)
                if (preceding.contains(config.elements[i].kind))
                    insertAt = i + 1;
            config.elements.insert(insertAt, spec);
        }
        const Mapping kindMapping = defaultKindMappings().value(kind);
        for (auto it = kindMapping.cbegin(); it != kindMapping.cend(); ++it)
            if (!config.classMapping.contains(it.key()))
                config.classMapping.insert(it.key(), it.value());
    }
}

QString renderTag(const TagConfig &config, const QHash<QString, QString> &values)
{
    if (config.elements.isEmpty())
        return {};
    const QString sep = s(textFor<TextOption>(kSeparators, config.separator, &TextOption::text, "-"));
    const QString open = s(textFor<Enclosing>(kEnclosings, config.enclosing, &Enclosing::open, "["));
    const QString close = s(textFor<Enclosing>(kEnclosings, config.enclosing, &Enclosing::close, "]"));
    const QString usageSep = s(textFor<TextOption>(kSeparators, config.usageSeparator, &TextOption::text, "|"));

    QStringList parts;
    for (const ElementSpec &el : config.elements) {
        if (!el.enabled)
            continue;
        const QString raw = values.value(el.kind);
        if (el.kind == u"usage") {
            QStringList styled;
            for (const auto usageNames = raw.split(kUsageInputSep, Qt::SkipEmptyParts);
                 const QString &name : usageNames)
                if (const QString v = styleValue(el.kind, el.style, name, config.classMapping); !v.isEmpty())
                    styled << v;
            if (!styled.isEmpty())
                parts << styled.join(usageSep);
            continue;
        }
        if (const QString v = styleValue(el.kind, el.style, raw, config.classMapping); !v.isEmpty())
            parts << v;
    }
    if (parts.isEmpty())
        return {};
    return open + parts.join(sep) + close;
}

QString joinTag(const QString &name, const QString &tag, const QString &placement)
{
    if (tag.isEmpty())
        return name;
    return placement == u"append" ? name + u' ' + tag : tag + u' ' + name;
}

bool routeEnabled(const TagConfig *config)
{
    if (!config)
        return false;
    return std::any_of(config->elements.cbegin(), config->elements.cend(),
                       [](const ElementSpec &e) { return e.kind == u"route" && e.enabled; });
}

QString renderRoute(const QString &from, const QString &to, const QString &arrowKey, bool fromMany, bool toMany)
{
    QString arrow;
    if (arrowKey == u"shape")
        arrow = QString(fromMany ? u'=' : u'-') + u'>' + (toMany ? u'=' : u'-');
    else
        arrow = s(textFor<TextOption>(kRouteArrows, arrowKey, &TextOption::text, ">"));
    if (!from.isEmpty() && !to.isEmpty())
        return from + u' ' + arrow + u' ' + to;
    if (!from.isEmpty())
        return QStringLiteral("from ") + from;
    if (!to.isEmpty())
        return QStringLiteral("to ") + to;
    return {};
}

QString applyMissionTitle(const QString &original, const QString &route, const TagConfig &config)
{
    if (route.isEmpty())
        return original;
    const QString sep = s(textFor<TextOption>(kTitleSeparators, config.titleSeparator, &TextOption::text, " - "));
    if (config.placement == u"replace")
        return route;
    if (config.placement == u"append")
        return original + sep + route;
    return route + sep + original;
}

QString abbreviateTitle(const QString &title, const QSet<QString> &enabled, const QString &rankSeparator,
                        bool standardizeHauling)
{
    QString out = title;
    const QString sep = s(textFor<TextOption>(kTitleSeparators, rankSeparator, &TextOption::text, ""));
    const bool removeRank = enabled.contains(QStringLiteral("rank"));
    if (standardizeHauling)
        out = standardizeHaulingTitle(out, removeRank, sep);

    auto apply = [&](std::span<const PhraseOption> options) {
        for (const PhraseOption &o : options) {
            const QString key = s(o.key);
            if (!enabled.contains(key) || key == u"rank")
                continue;
            const QString phrase = s(o.phrase);
            QString replacement = s(o.replacement);
            if (key == u"hauler_needed_for") {
                // Same slot as the " Rank -" triggers: keep the word "Rank"
                // unless it's being removed, then the rank separator.
                replacement = removeRank ? sep : QStringLiteral("Rank") + sep;
            } else if (key == u"ling_family_rank") {
                const QString fragment = removeRank ? u' ' + sep : QStringLiteral(" Rank") + sep;
                replacement = QStringLiteral("~mission(ReputationRank)") + fragment + QStringLiteral("Cargo");
            }
            out = phrase.contains(u' ') ? out.replace(phrase, replacement) : replaceWord(out, phrase, replacement);
        }
    };
    apply(kShortenPhrases);
    apply(kRemoveWords);
    apply(kUnderline);

    // The punctuation after "Rank" always follows the rank separator.
    for (const char *trigger : kRankTriggers) {
        const QString t = s(trigger);
        if (!out.contains(t))
            continue;
        const QString fragment = removeRank ? u' ' + sep : QStringLiteral(" Rank") + sep;
        out.replace(t, fragment);
    }

    out = collapseWhitespace(out);
    qsizetype end = out.size();
    while (end > 0 && QStringView(u" -,|:").contains(out[end - 1]))
        --end;
    out.truncate(end);
    return out;
}

QString fingerprint(const QMap<QString, TagConfig> &configs, bool annotateMissionDescs)
{
    QStringList items;
    for (auto it = configs.cbegin(); it != configs.cend(); ++it)
        items << py::jsonString(it.key(), true) + QStringLiteral(": ") + py::jsonString(it.value().toJson(), true);
    const QString blob = QStringLiteral("{\"annotate_mission_descs\": %1, \"configs\": {%2}}")
                             .arg(py::jsonBool(annotateMissionDescs), items.join(QStringLiteral(", ")));
    return QString::fromLatin1(QCryptographicHash::hash(blob.toUtf8(), QCryptographicHash::Sha256).toHex().left(12));
}

} // namespace core::tags
