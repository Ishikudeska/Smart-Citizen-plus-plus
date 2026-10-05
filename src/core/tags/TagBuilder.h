#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

#include <array>
#include <optional>
#include <span>

// The Tag Builder: the bracketed annotations ("[MIL-S2-A]", "[E-S1]",
// "[CF|QDRV]") the enhancements generator puts on component, missile,
// ship-weapon and commodity names, plus the mission-title route and title
// shortening. Qt Core only, no settings access; shared by the generator and
// the Enhancements page preview. Ports src/utils/tag_builder.py. The default
// configs reproduce the pre-Tag-Builder output byte for byte.
namespace core::tags {

inline const QStringList kCategories = {QStringLiteral("components"), QStringLiteral("missiles"),
                                        QStringLiteral("ship_weapons"), QStringLiteral("commodities"),
                                        QStringLiteral("mission_titles")};

// Separator inside a values-map "usage" entry (between category names).
inline constexpr QChar kUsageInputSep = QChar(0x1F);

// Element kinds each category supports, in default order.
QStringList elementKinds(const QString &category);

// (key, label) choices for an element kind's style; first is the default.
struct Option
{
    const char *key;
    const char *label;
};
std::span<const Option> stylesFor(const QString &kind);
QString elementLabel(const QString &kind);

// (key, label, rendered text) tables.
struct TextOption
{
    const char *key;
    const char *label;
    const char *text;
};
std::span<const TextOption> separators();
std::span<const TextOption> routeArrows();
std::span<const TextOption> titleSeparators(); // also the rank separators
struct Enclosing
{
    const char *key;
    const char *label;
    const char *open;
    const char *close;
};
std::span<const Enclosing> enclosings();
std::span<const Option> placements();             // prepend / append
std::span<const Option> missionTitlePlacements(); // append / prepend / replace
std::span<const Option> locationDetails();        // address / name

// Title-shortening checkboxes: (key, label, phrase, replacement).
struct PhraseOption
{
    const char *key;
    const char *label;
    const char *phrase;
    const char *replacement;
};
std::span<const PhraseOption> shortenPhraseOptions();
std::span<const PhraseOption> removeWordOptions();
std::span<const PhraseOption> underlineOptions();

// Cargo-grade words and their abbreviations, longest first.
std::span<const std::pair<const char *, const char *>> sizeAbbreviations();

// Crafting usage categories: (name, short, med, long, legend group).
struct UsageCategory
{
    const char *name;
    const char *shortCode;
    const char *medCode;
    const char *longCode;
    const char *group;
};
std::span<const UsageCategory> craftUsageCategories();

// The generator's compact damage labels -> the mapping's full names.
QString damageMappingKey(const QString &compactLabel);

using Variants = std::array<QString, 3>; // short, medium, long
using Mapping = QMap<QString, Variants>;

// Default variant mappings per mapped kind ("class", "type", "ordinance",
// "damage", "label", "collection", "usage").
const QHash<QString, Mapping> &defaultKindMappings();
bool isMappedKind(const QString &kind);

struct ElementSpec
{
    QString kind;
    bool enabled = true;
    QString style; // empty: the first style option
    bool operator==(const ElementSpec &) const = default;
};

struct TagConfig
{
    QList<ElementSpec> elements;
    QString separator = QStringLiteral("hyphen");
    QString enclosing = QStringLiteral("square");
    QString placement = QStringLiteral("prepend");
    Mapping classMapping;
    QString usageSeparator = QStringLiteral("pipe");
    QString routeArrow = QStringLiteral("gt");
    QString titleSeparator = QStringLiteral("dash");
    QString locationDetail = QStringLiteral("address");
    QSet<QString> abbreviatedPhrases;
    QString rankSeparator = QStringLiteral("dash");
    QSet<QString> shortenedSizes;
    bool standardizeHaulingNames = false;

    bool operator==(const TagConfig &) const = default;

    // json.dumps(asdict(cfg), ensure_ascii=False, sort_keys=True), with the
    // sets as sorted lists: the stored form and the fingerprint input.
    QString toJson() const;
    static std::optional<TagConfig> fromJson(const QString &json);
    // TagConfig.from_dict, including the legacy "abbreviate_title" bool.
    static TagConfig fromObject(const QJsonObject &object);
};

TagConfig defaultConfig(const QString &category);

// Settings-load upgrades: ship-weapon damage keys renamed to full words, and
// element kinds added in later versions inserted at their canonical
// position (with their default mappings).
void migrateMapping(const QString &category, TagConfig &config);
void backfillNewElements(const QString &category, TagConfig &config);

// The tag for `values` (kind -> raw value), with its brackets, or empty when
// no enabled element has a value.
QString renderTag(const TagConfig &config, const QHash<QString, QString> &values);

// Name and tag joined by one space: the tag goes after the name only for
// placement "append", before it otherwise.
QString joinTag(const QString &name, const QString &tag, const QString &placement);

bool routeEnabled(const TagConfig *config);
QString renderRoute(const QString &from, const QString &to, const QString &arrowKey, bool fromMany = false,
                    bool toMany = false);
QString applyMissionTitle(const QString &original, const QString &route, const TagConfig &config);

// Shortens a stock mission title per the enabled option keys; game tokens
// (~mission(...)) are never touched.
QString abbreviateTitle(const QString &title, const QSet<QString> &enabled = {},
                        const QString &rankSeparator = QStringLiteral("dash"),
                        bool standardizeHauling = false);

// 12 hex characters identifying the whole Tag Builder state.
QString fingerprint(const QMap<QString, TagConfig> &configs, bool annotateMissionDescs);

} // namespace core::tags
