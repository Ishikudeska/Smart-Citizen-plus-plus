#include "core/enhancements/Lookups.h"

#include "core/text/PyFormat.h"
#include "core/text/PyText.h"

#include <QSet>

#include <array>
#include <cmath>

namespace core::enh {

namespace {

// Path parts below the records root, lower-cased, file name included.
QSet<QString> relativeParts(const RecordStore &store, const QString &path)
{
    QString rel = path.mid(store.recordsDir().size());
    while (rel.startsWith(u'/'))
        rel.remove(0, 1);
    QSet<QString> parts;
    for (const auto segments = rel.split(u'/', Qt::SkipEmptyParts); const QString &p : segments)
        parts.insert(p.toLower());
    return parts;
}

std::string_view lstripAt(std::string_view s)
{
    while (!s.empty() && s.front() == '@')
        s.remove_prefix(1);
    return s;
}

} // namespace

RecordLookup buildAmmoLookup(const RecordStore &store, const QString &relDir)
{
    RecordLookup lookup;
    if (!store.dirExists(relDir))
        return lookup;
    for (const auto files = store.indexRglob(relDir); const QString &file : files) {
        XmlDoc doc = XmlDoc::load(file);
        if (!doc)
            continue;
        const Node root = doc.root();
        const std::string_view ref = getOr(root, "__ref");
        lookup.byId.insert(ref.empty() ? fileStem(file) : qs(ref), root);
        lookup.docs.push_back(std::move(doc));
    }
    return lookup;
}

ScitemLookups buildScitemLookups(const RecordStore &store, const Loc &loc, const tags::TagConfig *componentsConfig)
{
    ScitemLookups out;
    const QString scitem = QStringLiteral("entities/scitem");
    if (!store.dirExists(scitem))
        return out;
    const std::optional<tags::TagConfig> miningCfg = miningLaserTagConfig(componentsConfig);
    // Subdir -> component type, in the Python dict's order.
    static const std::array<std::pair<const char *, const char *>, 5> subdirTypes = {{
        {"shieldgenerator", "Shield Generator"},
        {"cooler", "Cooler"},
        {"powerplant", "Power Plant"},
        {"quantumdrive", "Quantum Drive"},
        {"radar", "Radar"},
    }};
    static const QRegularExpression typeless(QStringLiteral(R"(^\[S\d)"));

    for (const auto files = store.indexRglob(scitem); const QString &file : files) {
        const XmlDoc doc = XmlDoc::load(file);
        if (!doc)
            continue;
        const Node root = doc.root();
        const QString ref = qs(getOr(root, "__ref"));
        const std::string_view rootTag = tag(root);
        const QString entityName =
            rootTag.find('.') != std::string_view::npos ? lastDotPart(rootTag) : fileStem(file);
        bool foundMag = false;
        bool foundName = false;
        bool foundDesc = false;
        std::optional<QString> displayName;
        QString descKey;
        forEachElement(root, [&](Node el) {
            if (!foundMag && polyType(el) == "SAmmoContainerComponentParams") {
                const std::string_view ammoRef = getOr(el, "ammoParamsRecord");
                if (!ammoRef.empty() && ammoRef != kNullUuid)
                    out.magazines.insert(entityName, {qs(ammoRef), qs(getOr(el, "maxAmmoCount"))});
                foundMag = true;
            }
            if (!foundName) {
                if (const std::string_view name = getOr(el, "Name"); name.starts_with('@')) {
                    const QString key = qs(lstripAt(name));
                    const QString *value = loc.find(key);
                    displayName = value ? *value : key;
                    if (!ref.isEmpty())
                        out.entityNames.insert(ref, *displayName);
                    foundName = true;
                }
            }
            if (!foundDesc) {
                const std::string_view desc = getOr(el, "Description");
                if (desc.starts_with('@') && !isSentinelLocRef(desc)) {
                    descKey = qs(lstripAt(desc));
                    foundDesc = true;
                }
            }
            return !(foundMag && foundName && foundDesc);
        });
        if (displayName && !displayName->isEmpty())
            out.entityNamesByFilename.insert(fileStem(file).toLower(), *displayName);

        if (ref.isEmpty() || descKey.isEmpty())
            continue;
        const QString desc = loc.value(descKey);
        const QSet<QString> parts = relativeParts(store, file);
        const bool inWeapons = parts.contains(QStringLiteral("weapons"));
        // Weapons never carry a component tag (#220), except ship-mounted
        // mining lasers (#266).
        const bool miningLaser = inWeapons && findDescendant(root, "SEntityComponentMiningLaserParams");
        if (desc.isEmpty() || (inWeapons && !miningLaser))
            continue;
        QString tag;
        if (miningLaser) {
            tag = miningLaserComponentTag(desc, root, miningCfg);
        } else {
            QString compType;
            for (const auto &[subdir, type] : subdirTypes)
                if (parts.contains(QString::fromLatin1(subdir))) {
                    compType = QString::fromLatin1(type);
                    break;
                }
            tag = componentNameTag(desc, root, componentsConfig, compType);
            if (tag.isEmpty())
                tag = bareTypeTagFromDesc(desc, componentsConfig);
        }
        // A typeless "[S1-A]" means nothing in a blueprint list (#160).
        if (!tag.isEmpty() && !typeless.match(tag).hasMatch())
            out.entityNameTags.insert(ref, tag);
    }
    return out;
}

RecordLookup buildControllerLookup(const RecordStore &store)
{
    RecordLookup lookup;
    const QString dir = QStringLiteral("entities/scitem/ships/controller");
    for (const auto files = store.glob(dir, QStringLiteral("controller_flight_*.xml"));
         const QString &file : files) {
        XmlDoc doc = XmlDoc::load(file);
        if (!doc)
            continue;
        const QString shipClass = fileStem(file).mid(int(std::string_view("controller_flight_").size()));
        lookup.byId.insert(shipClass.toLower(), doc.root());
        lookup.docs.push_back(std::move(doc));
    }
    return lookup;
}

RecordLookup buildArmorLookup(const RecordStore &store)
{
    RecordLookup lookup;
    for (const auto files = store.glob(QStringLiteral("entities/scitem/ships/armor"), QStringLiteral("*.xml"));
         const QString &file : files) {
        XmlDoc doc = XmlDoc::load(file);
        if (!doc)
            continue;
        lookup.byId.insert(recordClassName(doc.root(), fileStem(file)).toLower(), doc.root());
        lookup.docs.push_back(std::move(doc));
    }
    return lookup;
}

QHash<QString, qint64> buildReputationLookup(const RecordStore &store)
{
    QHash<QString, qint64> out;
    for (const auto files = store.rglob(QStringLiteral("reputation/rewards/missionrewards_reputation"));
         const QString &file : files) {
        const XmlDoc doc = XmlDoc::load(file);
        if (!doc)
            continue;
        const std::string_view uuid = getOr(doc.root(), "__ref");
        const std::string_view amount = getOr(doc.root(), "reputationAmount");
        if (uuid.empty() || amount.empty())
            continue;
        if (const std::optional<double> v = toFloat(amount); v && std::isfinite(*v))
            out.insert(qs(uuid), qint64(std::trunc(*v))); // int(float(...))
    }
    return out;
}

Standings buildStandings(const RecordStore &store, const Loc &englishLoc)
{
    Standings out;
    static const QRegularExpression trackRe(QStringLiteral(R"(^Rep(?:Standing|Scope)_([A-Za-z]+)_)"));
    for (const auto files = store.rglob(QStringLiteral("reputation/standings")); const QString &file : files) {
        const XmlDoc doc = XmlDoc::load(file);
        if (!doc)
            continue;
        const std::string_view uuid = getOr(doc.root(), "__ref");
        const std::string_view display = getOr(doc.root(), "displayName");
        if (uuid.empty() || !display.starts_with('@'))
            continue;
        const QString key = qs(lstripAt(display));
        const QString id = qs(uuid);
        if (const QString rank = englishLoc.value(key); !rank.isEmpty())
            out.ranks.insert(id, rank);
        if (const QRegularExpressionMatch m = trackRe.match(key); m.hasMatch()) {
            const QString family = m.captured(1);
            QString track = englishLoc.value(QStringLiteral("RepScope_%1_Name").arg(family));
            if (track.isEmpty())
                track = englishLoc.value(QStringLiteral("RepScope_%1_Name,P").arg(family));
            out.tracks.insert(id, track.isEmpty() ? family : track);
        }
    }
    return out;
}

} // namespace core::enh
