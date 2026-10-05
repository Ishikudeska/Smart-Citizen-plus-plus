#include "core/enhancements/Tags.h"

#include "core/enhancements/Stats.h"
#include "core/text/PyText.h"

#include <QMap>
#include <QMutex>

namespace core::enh {

namespace {

// Item Type -> abbreviation, the Class: stand-in for lean items. Ship-weapon
// types map to the strings-tab damage letters (E/B/D/EMP).
const QHash<QString, QString> &itemTypeAbbrev()
{
    static const QHash<QString, QString> table = {
        {QStringLiteral("Mining Laser"), QStringLiteral("MIN")},
        {QStringLiteral("Salvage Head"), QStringLiteral("SAL")},
        {QStringLiteral("Shield Generator"), QStringLiteral("SHLD")},
        {QStringLiteral("Cooler"), QStringLiteral("COOL")},
        {QStringLiteral("Power Plant"), QStringLiteral("POWR")},
        {QStringLiteral("Quantum Drive"), QStringLiteral("QDRV")},
        {QStringLiteral("Radar"), QStringLiteral("RADR")},
        {QStringLiteral("Bomb Rack"), QStringLiteral("BRK")},
        {QStringLiteral("Laser Beam"), QStringLiteral("E")},
        {QStringLiteral("Laser Cannon"), QStringLiteral("E")},
        {QStringLiteral("Laser Gatling"), QStringLiteral("E")},
        {QStringLiteral("Laser Mine"), QStringLiteral("E")},
        {QStringLiteral("Laser Repeater"), QStringLiteral("E")},
        {QStringLiteral("Laser Scattergun"), QStringLiteral("E")},
        {QStringLiteral("Laser Turret"), QStringLiteral("E")},
        {QStringLiteral("Neutron Cannon"), QStringLiteral("E")},
        {QStringLiteral("Neutron Repeater"), QStringLiteral("E")},
        {QStringLiteral("Plasma Cannon"), QStringLiteral("E")},
        {QStringLiteral("Plasma Canon"), QStringLiteral("E")}, // CIG typo
        {QStringLiteral("Plasma Scattergun"), QStringLiteral("E")},
        {QStringLiteral("Tachyon Cannon"), QStringLiteral("E")},
        {QStringLiteral("Ballistic Cannon"), QStringLiteral("B")},
        {QStringLiteral("Ballistic Cannon Turret"), QStringLiteral("B")},
        {QStringLiteral("Ballistic Gatling"), QStringLiteral("B")},
        {QStringLiteral("Ballistic Gatling (x2)"), QStringLiteral("B")},
        {QStringLiteral("Ballistic Gatling Gun"), QStringLiteral("B")},
        {QStringLiteral("Ballistic Gatling Turret"), QStringLiteral("B")},
        {QStringLiteral("Ballistic Repeater"), QStringLiteral("B")},
        {QStringLiteral("Ballistic Scattergun"), QStringLiteral("B")},
        {QStringLiteral("Mass Driver Cannon"), QStringLiteral("B")},
        {QStringLiteral("Railgun"), QStringLiteral("B")},
        {QStringLiteral("Distortion Cannon"), QStringLiteral("D")},
        {QStringLiteral("Distortion Repeater"), QStringLiteral("D")},
        {QStringLiteral("Distortion Scattergun"), QStringLiteral("D")},
        {QStringLiteral("EMP Generator"), QStringLiteral("EMP")},
    };
    return table;
}

// "Size: 1" or "Size: S0" in description text; the digits only.
const QRegularExpression &sizeLineRe()
{
    static const QRegularExpression re = py::re(QStringLiteral(R"(Size:\s*S?(\d+))"));
    return re;
}

const QRegularExpression &itemTypeLineRe()
{
    static const QRegularExpression re = py::re(QStringLiteral(R"(Item Type:\s*([^\\\n]+?)\s*(?:\\n|\n|$))"));
    return re;
}

QString camelSplit(QString s)
{
    static const QRegularExpression re(QStringLiteral("([a-z])([A-Z])"));
    return s.replace(re, QStringLiteral("\\1 \\2"));
}

bool isAsciiDigits(std::string_view s)
{
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

const tags::TagConfig &orDefault(const tags::TagConfig *config, const char *category)
{
    return config ? *config : defaultTagConfig(QString::fromLatin1(category));
}

// "_S0*(\d+)_" on the record's Name attribute, as a plain number.
QString sizeFromNameAttr(Node root)
{
    std::string_view name = getOr(root, "Name");
    if (name.empty())
        name = getOr(root, "name");
    static const QRegularExpression re = py::re(QStringLiteral(R"(_S0*(\d+)_)"));
    const QRegularExpressionMatch m = re.match(qs(name));
    return m.hasMatch() ? QString::number(m.capturedView(1).toLongLong()) : QString();
}

QString sizeFromDesc(const QString &desc)
{
    const QRegularExpressionMatch m = sizeLineRe().match(desc);
    return m.hasMatch() ? m.captured(1) : QString();
}

} // namespace

const tags::TagConfig &defaultTagConfig(const QString &category)
{
    static QMutex mutex;
    static QMap<QString, tags::TagConfig> cache;
    QMutexLocker lock(&mutex);
    auto it = cache.find(category);
    if (it == cache.end())
        it = cache.insert(category, tags::defaultConfig(category));
    return *it;
}

QString componentNameTag(const QString &desc, Node root, const tags::TagConfig *config,
                         const QString &componentType)
{
    Node attachDef;
    if (root) {
        if (const Node attach = findDescendant(root, "SAttachableComponentParams"))
            attachDef = find(attach, "AttachDef");
    }

    // Size: the description, then the class name, the Name loc key, AttachDef.
    QString size = sizeFromDesc(desc);
    bool haveSize = sizeLineRe().match(desc).hasMatch();
    if (!haveSize && root) {
        const QString xmlSize = extractItemSize(lastDotPart(tag(root)));
        if (!xmlSize.isEmpty()) {
            size = xmlSize.sliced(1);
            haveSize = true;
        }
    }
    if (!haveSize && root) {
        if (const QString nameKey = locNameKey(root); !nameKey.isEmpty()) {
            const QString keySize = extractItemSize(nameKey);
            if (!keySize.isEmpty()) {
                size = keySize.sliced(1);
                haveSize = true;
            }
        }
    }
    if (!haveSize && attachDef) {
        if (const std::string_view raw = getOr(attachDef, "Size"); isAsciiDigits(raw)) {
            size = qs(raw);
            haveSize = true;
        }
    }
    if (!haveSize)
        return QString();

    static const QRegularExpression gradeRe = py::re(QStringLiteral(R"(Grade:\s*([A-D]))"));
    static const QRegularExpression classRe = py::re(QStringLiteral(R"(Class:\s*(\w+))"));
    const QRegularExpressionMatch gradeM = gradeRe.match(desc);
    const QRegularExpressionMatch classM = classRe.match(desc);

    QString attachGrade;
    QString attachClass;
    if (!gradeM.hasMatch() && attachDef) {
        if (const std::string_view num = getOr(attachDef, "Grade"); isAsciiDigits(num)) {
            const int index = qs(num).toInt() - 1;
            if (index >= 0 && index <= 3)
                attachGrade = QString(QChar(u'A' + index));
        }
    }
    if (!classM.hasMatch() && attachDef) {
        if (const std::string_view subtype = getOr(attachDef, "SubType"); !subtype.empty())
            attachClass = camelSplit(qs(subtype));
    }
    const QString grade = gradeM.hasMatch() ? gradeM.captured(1) : attachGrade;
    const QString className = classM.hasMatch() ? classM.captured(1) : attachClass;

    QString xmlItemType;
    if (className.isEmpty() && root) {
        if (const Node icp = findDescendant(root, "ItemComponentParams"))
            xmlItemType = camelSplit(qs(getOr(icp, "itemType")));
    }

    // Strict path: the full trio with a known class goes through the Tag
    // Builder, so the user's component config applies.
    const tags::Mapping &classes = tags::defaultKindMappings().value(QStringLiteral("class"));
    if (!grade.isEmpty() && !className.isEmpty() && classes.contains(className)) {
        QHash<QString, QString> values = {
            {QStringLiteral("class"), className},
            {QStringLiteral("size"), size},
            {QStringLiteral("grade"), grade},
        };
        if (!componentType.isEmpty())
            values.insert(QStringLiteral("type"), componentType);
        const QString out = tags::renderTag(orDefault(config, "components"), values);
        if (!out.isEmpty())
            return out;
        // Every element disabled: the legacy hardcoded shape.
        return QStringLiteral("[%1-S%2-%3]").arg(classes.value(className)[1], size, grade);
    }

    // Fallback: classify by Item Type when Class: is missing or unknown.
    QString typeAbbrev;
    if (const QRegularExpressionMatch m = itemTypeLineRe().match(desc); m.hasMatch())
        typeAbbrev = itemTypeAbbrev().value(py::strip(m.captured(1)));
    if (typeAbbrev.isEmpty() && !xmlItemType.isEmpty())
        typeAbbrev = itemTypeAbbrev().value(xmlItemType);
    if (typeAbbrev.isEmpty() && !className.isEmpty())
        typeAbbrev = itemTypeAbbrev().value(className);
    if (typeAbbrev.isEmpty())
        return QString();

    QStringList parts = {typeAbbrev, QStringLiteral("S") + size};
    if (!grade.isEmpty())
        parts << grade;
    return u'[' + parts.join(u'-') + u']';
}

QString missileNameTag(const QString &desc, Node root, const tags::TagConfig *config)
{
    static const QStringList trackingRaw = {QStringLiteral("CrossSection"), QStringLiteral("Electromagnetic"),
                                            QStringLiteral("Infrared")};
    QString size = sizeFromDesc(desc);
    bool haveSize = sizeLineRe().match(desc).hasMatch();
    QString seeker;
    bool isBomb = false;
    if (root) {
        forEachElement(root, [&](Node el) {
            const std::string_view t = tag(el);
            if (t.ends_with("AttachDef")) {
                if (!haveSize) {
                    if (const std::string_view raw = getOr(el, "Size"); isAsciiDigits(raw)) {
                        size = qs(raw);
                        haveSize = true;
                    }
                }
                if (!isBomb && getOr(el, "Type") == "Bomb")
                    isBomb = true;
            }
            if (!isBomb && t.ends_with("SCItemBombParams"))
                isBomb = true;
            if (seeker.isEmpty() && (t.ends_with("targetingParams") || t.ends_with("TargetingParams"))) {
                if (const auto raw = get(el, "trackingSignalType"); raw && trackingRaw.contains(qs(*raw)))
                    seeker = qs(*raw);
            }
            return !(isBomb && !seeker.isEmpty() && haveSize);
        });
    }
    if (!haveSize)
        return QString();
    if (seeker.isEmpty() && !isBomb) {
        static const QRegularExpression re =
            py::re(QStringLiteral(R"(Tracking Signal:\s*([A-Za-z ]+?)(?:\\n|\n|$))"));
        if (const QRegularExpressionMatch m = re.match(desc); m.hasMatch()) {
            const QString normalized = m.captured(1).remove(u' ');
            for (const QString &raw : trackingRaw)
                if (normalized.toLower() == raw.toLower()) {
                    seeker = raw;
                    break;
                }
        }
    }
    const QString ordinance = isBomb ? QStringLiteral("Bomb") : seeker;
    return tags::renderTag(orDefault(config, "missiles"),
                           {{QStringLiteral("ordinance"), ordinance}, {QStringLiteral("size"), size}});
}

const tags::ElementSpec *componentElement(const tags::TagConfig &config, const QString &kind)
{
    for (const tags::ElementSpec &el : config.elements)
        if (el.kind == kind)
            return &el;
    return nullptr;
}

std::optional<tags::TagConfig> miningLaserTagConfig(const tags::TagConfig *componentsConfig)
{
    if (!componentsConfig)
        return std::nullopt;
    const tags::ElementSpec *typeEl = componentElement(*componentsConfig, QStringLiteral("type"));
    if (!typeEl || !typeEl->enabled)
        return std::nullopt;
    tags::TagConfig cfg;
    cfg.elements = {
        {QStringLiteral("type"), true, typeEl->style.isEmpty() ? QStringLiteral("med") : typeEl->style}};
    const tags::ElementSpec *sizeEl = componentElement(*componentsConfig, QStringLiteral("size"));
    if (sizeEl && sizeEl->enabled)
        cfg.elements.append(
            {QStringLiteral("size"), true, sizeEl->style.isEmpty() ? QStringLiteral("sn") : sizeEl->style});
    cfg.separator = componentsConfig->separator;
    cfg.enclosing = componentsConfig->enclosing;
    cfg.placement = componentsConfig->placement;
    cfg.classMapping = componentsConfig->classMapping;
    return cfg;
}

QString miningLaserComponentTag(const QString &desc, Node root,
                                const std::optional<tags::TagConfig> &miningConfig)
{
    if (!miningConfig || !root)
        return QString();
    if (!findDescendant(root, "SEntityComponentMiningLaserParams"))
        return QString();
    QString size = sizeFromNameAttr(root);
    if (size.isEmpty())
        size = sizeFromDesc(desc);
    return tags::renderTag(*miningConfig, {{QStringLiteral("type"), QStringLiteral("Mining Laser")},
                                           {QStringLiteral("size"), size}});
}

NameTagger shipWeaponNameTagger(const RecordLookup &ammo, const tags::TagConfig *config,
                                const tags::TagConfig *miningLaserConfig)
{
    const tags::TagConfig cfg = orDefault(config, "ship_weapons");
    const std::optional<tags::TagConfig> miningCfg =
        miningLaserTagConfig(&orDefault(miningLaserConfig, "components"));
    return [&ammo, cfg, miningCfg](const QString &desc, Node root) -> QString {
        if (!root)
            return QString();
        QString size = sizeFromNameAttr(root);
        if (size.isEmpty())
            size = sizeFromDesc(desc);

        // The ammo's largest damage type, by its full mapping name.
        QString damage;
        const Node container = find(root, ".//SAmmoContainerComponentParams");
        const auto ammoId = container ? get(container, "ammoParamsRecord") : std::nullopt;
        if (ammoId && !ammoId->empty() && *ammoId != kNullUuid) {
            if (const Node ammoRoot = ammo.value(qs(*ammoId))) {
                const DamageBreakdown breakdown = ammoDamageBreakdown(ammoRoot);
                if (!breakdown.parts.empty()) {
                    auto best = breakdown.parts.begin();
                    for (auto it = breakdown.parts.begin(); it != breakdown.parts.end(); ++it)
                        if (it->second > best->second)
                            best = it;
                    damage = tags::damageMappingKey(best->first);
                }
            }
        }
        // EMP devices and tractor beams have no damage: no tag, except that
        // mining lasers get the component Type+Size shape (#266).
        if (damage.isEmpty())
            return miningLaserComponentTag(desc, root, miningCfg);
        return tags::renderTag(cfg, {{QStringLiteral("damage"), damage}, {QStringLiteral("size"), size}});
    };
}

QString bareTypeTagFromDesc(const QString &desc, const tags::TagConfig *componentsConfig)
{
    if (!componentsConfig)
        return QString();
    const tags::ElementSpec *typeEl = componentElement(*componentsConfig, QStringLiteral("type"));
    if (!typeEl || !typeEl->enabled)
        return QString();
    const QRegularExpressionMatch m = itemTypeLineRe().match(desc);
    const QString typeName = m.hasMatch() ? py::strip(m.captured(1)) : QString();
    if (typeName != u"Fuel Nozzle")
        return QString();
    tags::TagConfig cfg;
    cfg.elements = {
        {QStringLiteral("type"), true, typeEl->style.isEmpty() ? QStringLiteral("med") : typeEl->style}};
    cfg.separator = componentsConfig->separator;
    cfg.enclosing = componentsConfig->enclosing;
    cfg.placement = componentsConfig->placement;
    cfg.classMapping = componentsConfig->classMapping;
    return tags::renderTag(cfg, {{QStringLiteral("type"), typeName}});
}

namespace {

// key[:-len("_Name")] + "_Desc" for a key ending in "_name" (any case).
QString descKeyFor(const QString &nameKey)
{
    return nameKey.chopped(5) + QStringLiteral("_Desc");
}

} // namespace

Loc bareTypeTags(const Loc &loc, const tags::TagConfig *componentsConfig)
{
    const tags::TagConfig &cfg = orDefault(componentsConfig, "components");
    Loc out;
    for (const auto &[key, name] : loc) {
        if (name.isEmpty() || !key.endsWith(QStringLiteral("_name"), Qt::CaseInsensitive))
            continue;
        const QString desc = loc.value(descKeyFor(key));
        if (desc.isEmpty())
            continue;
        const QString tag = bareTypeTagFromDesc(desc, &cfg);
        if (tag.isEmpty())
            continue;
        out.insert(key, tags::joinTag(name, tag, cfg.placement));
        const QString shortKey = key + QStringLiteral("_short");
        if (const QString shortValue = loc.value(shortKey); !shortValue.isEmpty())
            out.insert(shortKey, tags::joinTag(shortValue, tag, cfg.placement));
    }
    return out;
}

QHash<QString, QString> bareTypeNameTagLookup(const Loc &loc, const tags::TagConfig *componentsConfig)
{
    QHash<QString, QString> out;
    if (!componentsConfig)
        return out;
    for (const auto &[key, name] : loc) {
        if (name.isEmpty() || !key.endsWith(QStringLiteral("_name"), Qt::CaseInsensitive))
            continue;
        const QString desc = loc.value(descKeyFor(key));
        if (desc.isEmpty())
            continue;
        if (const QString tag = bareTypeTagFromDesc(desc, componentsConfig); !tag.isEmpty())
            out.insert(name, tag);
    }
    return out;
}

} // namespace core::enh
