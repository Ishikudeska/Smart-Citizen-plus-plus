#include "EnhancementsController.h"

#include "AppController.h"

#include "core/i18n/Translator.h"
#include "core/model/Enhancements.h"
#include "core/pipeline/Extraction.h"

#include <QDir>
#include <QFileInfo>

using namespace core;
namespace tg = core::tags;

namespace {

QString text(const char *key, const QVariantHash &args = {})
{
    return core::i18n::tr(key, args);
}

QString q(const char *s)
{
    return QString::fromUtf8(s);
}

QVariantList optionList(auto span)
{
    QVariantList out;
    for (const auto &o : span)
        out << QVariantMap{{QStringLiteral("key"), q(o.key)}, {QStringLiteral("label"), q(o.label)}};
    return out;
}

const QHash<QString, QHash<QString, QString>> &previewValues()
{
    static const QHash<QString, QHash<QString, QString>> values = {
        {QStringLiteral("components"),
         {{QStringLiteral("class"), QStringLiteral("Military")}, {QStringLiteral("size"), QStringLiteral("2")},
          {QStringLiteral("grade"), QStringLiteral("A")}, {QStringLiteral("type"), QStringLiteral("Shield Generator")}}},
        {QStringLiteral("missiles"), {{QStringLiteral("ordinance"), QStringLiteral("Infrared")}, {QStringLiteral("size"), QStringLiteral("1")}}},
        {QStringLiteral("ship_weapons"), {{QStringLiteral("damage"), QStringLiteral("Energy")}, {QStringLiteral("size"), QStringLiteral("2")}}},
        {QStringLiteral("commodities"),
         {{QStringLiteral("label"), QStringLiteral("Crafting")},
          {QStringLiteral("usage"), QStringLiteral("Quantum Drive") + tg::kUsageInputSep + QStringLiteral("Shield")},
          {QStringLiteral("collection"), QStringLiteral("Collection")}}},
    };
    return values;
}

const QHash<QString, QString> &previewNames()
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("components"), QStringLiteral("FR-76")},
        {QStringLiteral("missiles"), QStringLiteral("Marksman I Missile")},
        {QStringLiteral("ship_weapons"), QStringLiteral("MaxOx NN-14")},
        {QStringLiteral("commodities"), QStringLiteral("Agricium")},
    };
    return names;
}

const char *const kCategoryDescKeys[][2] = {
    {"ships", "enhancements.cat_desc_ships"},
    {"ship_items", "enhancements.cat_desc_ship_items"},
    {"gear", "enhancements.cat_desc_gear"},
    {"missions", "enhancements.cat_desc_missions"},
    {"commodities", "enhancements.cat_desc_commodities"},
    {"journal", "enhancements.cat_desc_journal"},
    {"medical_consumables", "enhancements.cat_desc_medical_consumables"},
};

const char *const kMissionFieldLabels[][2] = {
    {"mission_type", "enhancements.mission_field_mission_type"},
    {"difficulty", "enhancements.mission_field_difficulty"},
    {"spawns", "enhancements.mission_field_hostiles"},
    {"reputation", "enhancements.mission_field_reputation"},
    {"blueprints", "enhancements.mission_field_blueprints"},
    {"ace", "enhancements.mission_field_ace_pilot"},
    {"resource_signatures", "enhancements.mission_field_resource_signatures"},
};

} // namespace

EnhancementsController::EnhancementsController(QObject *parent) : QObject(parent)
{
    for (const auto &c : enhancements::categories())
        stagedCategories_.insert(q(c.id), app().settings().enhancementCategoryEnabled(q(c.id)));
    saved_ = configs_ = app().settings().allTagConfigs();
    savedAnnotate_ = annotate_ = app().settings().annotateMissionDescs();
    for (const QString &f : kMissionTitleTagKeys)
        savedTitleTags_[f] = titleTags_[f] = app().settings().missionTitleTag(f);
    generateDirty_ = app().dataForgeStatus() != u"fresh";
    if (!generateDirty_) {
        const QString dir = app().paths().enhancementsDir();
        for (const auto ids = app().settings().enabledEnhancementFileIds(); const QString &id : ids)
            if (!QFileInfo::exists(QDir(dir).filePath(enhancements::fileNameFor(id))))
                generateDirty_ = true;
    }
    connect(&app(), &AppController::pathsChanged, this, &EnhancementsController::changed);
    connect(&app(), &AppController::entriesReloaded, this, &EnhancementsController::changed);
    connect(app().tasks(), &TaskRunner::finished, this, [this](const QString &title) {
        if (title == text("progress.generating_enhancements_title")) {
            generateDirty_ = false;
            emit changed();
        }
    });
}

AppController &EnhancementsController::app() const
{
    return *AppController::instance();
}

// ── categories and options ────────────────────────────────────────────────

QVariantList EnhancementsController::categories() const
{
    QVariantList out;
    const QString dir = app().paths().enhancementsDir();
    for (const auto &c : enhancements::categories()) {
        const QString id = q(c.id);
        QString desc;
        for (const auto &[key, descKey] : kCategoryDescKeys)
            if (id == q(key))
                desc = text(descKey);
        bool present = true;
        for (const QString &file : c.fileIds)
            present = present && QFileInfo::exists(QDir(dir).filePath(enhancements::fileNameFor(file)));
        out << QVariantMap{{QStringLiteral("id"), id},
                           {QStringLiteral("label"), c.label},
                           {QStringLiteral("description"), desc},
                           {QStringLiteral("enabled"), stagedCategories_.value(id, true)},
                           {QStringLiteral("generated"), present}};
    }
    return out;
}

bool EnhancementsController::categoriesDirty() const
{
    for (auto it = stagedCategories_.cbegin(); it != stagedCategories_.cend(); ++it)
        if (app().settings().enhancementCategoryEnabled(it.key()) != it.value())
            return true;
    return false;
}

void EnhancementsController::setCategoryEnabled(const QString &id, bool on)
{
    stagedCategories_[id] = on;
    markGenerateDirty();
}

void EnhancementsController::applyCategories()
{
    for (auto it = stagedCategories_.cbegin(); it != stagedCategories_.cend(); ++it)
        app().settings().setEnhancementCategoryEnabled(it.key(), it.value());
    emit changed();
    app().reload();
}

QVariantList EnhancementsController::missionFields() const
{
    QVariantList out;
    for (const auto &[field, labelKey] : kMissionFieldLabels) {
        const QString label = text(labelKey);
        out << QVariantMap{{QStringLiteral("id"), q(field)},
                           {QStringLiteral("label"), label},
                           {QStringLiteral("enabled"), app().settings().missionDetailField(q(field))},
                           {QStringLiteral("tooltip"), q(field) == u"ace"
                                                           ? text("enhancements.mission_field_ace_tooltip")
                                                           : text("enhancements.mission_field_default_tooltip", {{QStringLiteral("label"), label}})}};
    }
    return out;
}

void EnhancementsController::setMissionField(const QString &field, bool on)
{
    app().settings().setMissionDetailField(field, on);
    markGenerateDirty();
}

bool EnhancementsController::statsPrepend() const
{
    return app().settings().statsPrepend();
}

void EnhancementsController::setStatsPrepend(bool on)
{
    app().settings().setStatsPrepend(on);
    markGenerateDirty();
}

bool EnhancementsController::standardizeShipNames() const
{
    return app().settings().standardizeEarnableShipNames();
}

void EnhancementsController::setStandardizeShipNames(bool on)
{
    app().settings().setStandardizeEarnableShipNames(on);
    markGenerateDirty();
}

bool EnhancementsController::rsOreNames() const
{
    return app().settings().rsOreNameAnnotations();
}

void EnhancementsController::setRsOreNames(bool on)
{
    app().settings().setRsOreNameAnnotations(on);
    markGenerateDirty();
}

QString EnhancementsController::repXpLabel() const
{
    return app().settings().repXpLabel();
}

void EnhancementsController::setRepXpLabel(const QString &label)
{
    if (label == repXpLabel())
        return;
    app().settings().setRepXpLabel(label);
    markGenerateDirty();
}

QString EnhancementsController::headerEmTag() const
{
    return app().settings().missionHeaderEmTag();
}

void EnhancementsController::setHeaderEmTag(const QString &tag)
{
    if (tag == headerEmTag())
        return;
    app().settings().setMissionHeaderEmTag(tag);
    markGenerateDirty();
}

QString EnhancementsController::missionHeader(const QString &key) const
{
    return app().settings().missionHeader(key);
}

void EnhancementsController::setMissionHeader(const QString &key, const QString &value)
{
    if (value == missionHeader(key))
        return;
    app().settings().setMissionHeader(key, value);
    if (key == u"blueprints")
        app().strings()->setBlueprintHeader(value);
    markGenerateDirty();
}

bool EnhancementsController::generateDirty() const
{
    return generateDirty_ || app().dataForgeStatus() != u"fresh";
}

QString EnhancementsController::forgeStatus() const
{
    const QString s = app().dataForgeStatus();
    if (s == u"fresh")
        return text("enhancements.forge_status_up_to_date");
    if (s == u"stale")
        return text("enhancements.forge_status_outdated");
    return text("enhancements.forge_status_not_extracted");
}

void EnhancementsController::markGenerateDirty()
{
    generateDirty_ = true;
    emit changed();
}

void EnhancementsController::generate()
{
    // Unsaved category toggles count: generate what the page shows.
    for (auto it = stagedCategories_.cbegin(); it != stagedCategories_.cend(); ++it)
        app().settings().setEnhancementCategoryEnabled(it.key(), it.value());
    app().generateEnhancements();
}

// ── Tag Builder ───────────────────────────────────────────────────────────

tg::TagConfig &EnhancementsController::config(const QString &category)
{
    if (!configs_.contains(category))
        configs_.insert(category, tg::defaultConfig(category));
    return configs_[category];
}

const tg::TagConfig &EnhancementsController::config(const QString &category) const
{
    static const tg::TagConfig empty;
    const auto it = configs_.constFind(category);
    return it == configs_.cend() ? empty : *it;
}

void EnhancementsController::touchTags()
{
    ++revision_;
    emit tagChanged();
}

bool EnhancementsController::tagDirty() const
{
    return configs_ != saved_ || annotate_ != savedAnnotate_ || titleTags_ != savedTitleTags_;
}

void EnhancementsController::setAnnotateMissionDescs(bool on)
{
    annotate_ = on;
    touchTags();
}

QVariantList EnhancementsController::tagCategories() const
{
    return {
        QVariantMap{{QStringLiteral("id"), QStringLiteral("components")}, {QStringLiteral("label"), text("scx.tag_cat_components")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("missiles")}, {QStringLiteral("label"), text("scx.tag_cat_missiles")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("ship_weapons")}, {QStringLiteral("label"), text("scx.tag_cat_ship_weapons")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("commodities")}, {QStringLiteral("label"), text("scx.tag_cat_commodities")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("mission_titles")}, {QStringLiteral("label"), text("scx.tag_cat_mission_titles")}},
    };
}

QVariantList EnhancementsController::elements(const QString &category) const
{
    QVariantList out;
    const tg::TagConfig &c = config(category);
    for (qsizetype i = 0; i < c.elements.size(); ++i) {
        const tg::ElementSpec &e = c.elements[i];
        const auto styles = tg::stylesFor(e.kind);
        QString style = e.style;
        if (style.isEmpty() && !styles.empty())
            style = q(styles.front().key);
        out << QVariantMap{{QStringLiteral("kind"), e.kind},
                           {QStringLiteral("label"), tg::elementLabel(e.kind)},
                           {QStringLiteral("enabled"), e.enabled},
                           {QStringLiteral("style"), style},
                           {QStringLiteral("styles"), optionList(styles)},
                           {QStringLiteral("mapped"), tg::isMappedKind(e.kind)}};
    }
    return out;
}

void EnhancementsController::setElementEnabled(const QString &category, int index, bool on)
{
    auto &els = config(category).elements;
    if (index < 0 || index >= els.size() || els[index].enabled == on)
        return;
    els[index].enabled = on;
    touchTags();
}

void EnhancementsController::setElementStyle(const QString &category, int index, const QString &style)
{
    auto &els = config(category).elements;
    if (index < 0 || index >= els.size() || els[index].style == style)
        return;
    els[index].style = style;
    touchTags();
}

void EnhancementsController::moveElement(const QString &category, int index, int delta)
{
    auto &els = config(category).elements;
    const int to = index + delta;
    if (index < 0 || index >= els.size() || to < 0 || to >= els.size())
        return;
    els.swapItemsAt(index, to);
    touchTags();
}

QString EnhancementsController::option(const QString &category, const QString &field) const
{
    const tg::TagConfig &c = config(category);
    if (field == u"separator") return c.separator;
    if (field == u"enclosing") return c.enclosing;
    if (field == u"placement") return c.placement;
    if (field == u"usageSeparator") return c.usageSeparator;
    if (field == u"routeArrow") return c.routeArrow;
    if (field == u"titleSeparator") return c.titleSeparator;
    if (field == u"locationDetail") return c.locationDetail;
    if (field == u"rankSeparator") return c.rankSeparator;
    return {};
}

void EnhancementsController::setOption(const QString &category, const QString &field, const QString &value)
{
    tg::TagConfig &c = config(category);
    QString *target = field == u"separator"        ? &c.separator
                      : field == u"enclosing"      ? &c.enclosing
                      : field == u"placement"      ? &c.placement
                      : field == u"usageSeparator" ? &c.usageSeparator
                      : field == u"routeArrow"     ? &c.routeArrow
                      : field == u"titleSeparator" ? &c.titleSeparator
                      : field == u"locationDetail" ? &c.locationDetail
                      : field == u"rankSeparator"  ? &c.rankSeparator
                                                   : nullptr;
    if (!target || *target == value)
        return;
    *target = value;
    touchTags();
}

QVariantList EnhancementsController::choices(const QString &field) const
{
    if (field == u"separator" || field == u"usageSeparator")
        return optionList(tg::separators());
    if (field == u"enclosing") {
        QVariantList out;
        for (const auto &e : tg::enclosings())
            out << QVariantMap{{QStringLiteral("key"), q(e.key)}, {QStringLiteral("label"), q(e.label)}};
        return out;
    }
    if (field == u"placement")
        return optionList(tg::placements());
    if (field == u"missionPlacement")
        return optionList(tg::missionTitlePlacements());
    if (field == u"routeArrow")
        return optionList(tg::routeArrows());
    if (field == u"titleSeparator" || field == u"rankSeparator")
        return optionList(tg::titleSeparators());
    if (field == u"locationDetail")
        return optionList(tg::locationDetails());
    return {};
}

bool EnhancementsController::flag(const QString &category, const QString &field) const
{
    const tg::TagConfig &c = config(category);
    if (field == u"standardizeHauling")
        return c.standardizeHaulingNames;
    if (field == u"route")
        return tg::routeEnabled(&c);
    return false;
}

void EnhancementsController::setFlag(const QString &category, const QString &field, bool on)
{
    tg::TagConfig &c = config(category);
    if (field == u"standardizeHauling") {
        c.standardizeHaulingNames = on;
    } else if (field == u"route") {
        for (tg::ElementSpec &e : c.elements)
            if (e.kind == u"route")
                e.enabled = on;
    }
    touchTags();
}

QVariantList EnhancementsController::phraseOptions() const
{
    QVariantList out;
    for (const auto &o : tg::shortenPhraseOptions())
        out << QVariantMap{{QStringLiteral("key"), q(o.key)}, {QStringLiteral("label"), q(o.label)}};
    for (const auto &o : tg::removeWordOptions())
        out << QVariantMap{{QStringLiteral("key"), q(o.key)}, {QStringLiteral("label"), q(o.label)}};
    return out;
}

bool EnhancementsController::phraseEnabled(const QString &key) const
{
    return config(QStringLiteral("mission_titles")).abbreviatedPhrases.contains(key);
}

void EnhancementsController::setPhraseEnabled(const QString &key, bool on)
{
    auto &set = config(QStringLiteral("mission_titles")).abbreviatedPhrases;
    if (on)
        set.insert(key);
    else
        set.remove(key);
    touchTags();
}

bool EnhancementsController::sizesShortened() const
{
    return !config(QStringLiteral("mission_titles")).shortenedSizes.isEmpty();
}

void EnhancementsController::setSizesShortened(bool on)
{
    auto &set = config(QStringLiteral("mission_titles")).shortenedSizes;
    set.clear();
    if (on)
        for (const auto &[word, abbr] : tg::sizeAbbreviations())
            set.insert(q(word));
    touchTags();
}

bool EnhancementsController::titleTag(const QString &field) const
{
    return titleTags_.value(field, Settings::missionTitleTagDefault(field));
}

void EnhancementsController::setTitleTag(const QString &field, bool on)
{
    titleTags_[field] = on;
    touchTags();
}

QString EnhancementsController::preview(const QString &category) const
{
    const tg::TagConfig &c = config(category);
    if (category == u"mission_titles") {
        QString sample = tg::abbreviateTitle(QStringLiteral("Master Rank - Direct Medium Cargo Haul"), c.abbreviatedPhrases,
                                             c.rankSeparator, c.standardizeHaulingNames);
        for (const auto &[word, abbr] : tg::sizeAbbreviations())
            if (c.shortenedSizes.contains(q(word)))
                sample.replace(q(word), q(abbr));
        const auto display = [](QString s) {
            return s.replace(QStringLiteral("<EM3>DIRECT</EM3>"), QStringLiteral("<u>DIRECT</u>"));
        };
        if (!tg::routeEnabled(&c))
            return text("enhancements.mt_preview_route_off", {{QStringLiteral("display"), display(sample)}});
        const bool address = c.locationDetail == u"address";
        const QString route = tg::renderRoute(address ? QStringLiteral("Area18, Crusader") : QStringLiteral("Area18"),
                                              address ? QStringLiteral("Lorville, Hurston") : QStringLiteral("Lorville"),
                                              c.routeArrow);
        return text("enhancements.mt_preview_route_on",
                    {{QStringLiteral("display"), display(tg::applyMissionTitle(sample, route, c))}});
    }
    const QString tag = tg::renderTag(c, previewValues().value(category));
    const QString name = previewNames().value(category, QStringLiteral("Sample"));
    if (tag.isEmpty())
        return text("enhancements.tag_preview_no_tag", {{QStringLiteral("name"), name}});
    return text("enhancements.tag_preview",
                {{QStringLiteral("content"), c.placement == u"append" ? name + u' ' + tag : tag + u' ' + name}});
}

QStringList EnhancementsController::mappedValues(const QString &category, const QString &kind) const
{
    Q_UNUSED(category)
    return tg::defaultKindMappings().value(kind).keys();
}

QVariantList EnhancementsController::mapping(const QString &category, const QString &kind) const
{
    QVariantList out;
    const tg::TagConfig &c = config(category);
    for (const auto values = mappedValues(category, kind); const QString &raw : values) {
        const tg::Variants v = c.classMapping.value(raw, tg::defaultKindMappings().value(kind).value(raw));
        out << QVariantMap{{QStringLiteral("value"), raw},
                           {QStringLiteral("short"), v[0]},
                           {QStringLiteral("med"), v[1]},
                           {QStringLiteral("long"), v[2]}};
    }
    return out;
}

void EnhancementsController::setMappingText(const QString &category, const QString &raw, int column,
                                            const QString &value)
{
    if (column < 0 || column > 2)
        return;
    tg::TagConfig &c = config(category);
    tg::Variants v;
    if (c.classMapping.contains(raw))
        v = c.classMapping.value(raw);
    else
        for (const tg::Mapping &m : tg::defaultKindMappings())
            if (m.contains(raw))
                v = m.value(raw);
    if (v[column] == value)
        return;
    v[column] = value;
    c.classMapping.insert(raw, v);
    touchTags();
}

void EnhancementsController::resetMapping(const QString &category, const QString &kind)
{
    tg::TagConfig &c = config(category);
    const tg::Mapping defaults = tg::defaultKindMappings().value(kind);
    for (auto it = defaults.cbegin(); it != defaults.cend(); ++it)
        c.classMapping.insert(it.key(), it.value());
    touchTags();
}

void EnhancementsController::resetTagDefaults()
{
    for (const QString &category : tg::kCategories)
        configs_.insert(category, tg::defaultConfig(category));
    annotate_ = true;
    for (const QString &f : kMissionTitleTagKeys)
        titleTags_[f] = Settings::missionTitleTagDefault(f);
    touchTags();
}

void EnhancementsController::saveTagChanges()
{
    for (auto it = configs_.cbegin(); it != configs_.cend(); ++it)
        app().settings().setTagConfig(it.key(), it.value());
    app().settings().setAnnotateMissionDescs(annotate_);
    for (auto it = titleTags_.cbegin(); it != titleTags_.cend(); ++it)
        app().settings().setMissionTitleTag(it.key(), it.value());
    saved_ = configs_;
    savedAnnotate_ = annotate_;
    savedTitleTags_ = titleTags_;
    touchTags();
    generate();
}
