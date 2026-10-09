#include "core/loadout/Catalog.h"

#include "core/enhancements/Common.h"
#include "core/enhancements/RecordStore.h"
#include "core/missions/Places.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <memory>

namespace core::loadout {

using namespace Qt::StringLiterals;

using enh::children;
using enh::find;
using enh::findAll;
using enh::findDescendant;
using enh::get;
using enh::getOr;
using enh::iter;
using enh::Node;
using enh::qs;
using enh::XmlDoc;

namespace {

double num(Node n, const char *attr, double fallback = 0)
{
    if (!n)
        return fallback;
    const auto v = enh::toFloat(get(n, attr));
    return v && std::isfinite(*v) ? *v : fallback;
}

// The first of `attrs` the element has (attribute names vary in case
// between the vehicle XMLs).
double numAny(Node n, std::initializer_list<const char *> attrs, double fallback = 0)
{
    for (const char *a : attrs)
        if (get(n, a))
            return num(n, a, fallback);
    return fallback;
}

QString textAny(Node n, std::initializer_list<const char *> attrs)
{
    for (const char *a : attrs)
        if (const auto v = get(n, a))
            return qs(*v);
    return QString();
}

QStringList splitTags(const QString &s)
{
    static const QRegularExpression sep(QStringLiteral("[\\s,]+"));
    return s.split(sep, Qt::SkipEmptyParts);
}

Damage damageOf(Node info)
{
    Damage d;
    if (!info)
        return d;
    d.physical = num(info, "DamagePhysical");
    d.energy = num(info, "DamageEnergy");
    d.distortion = num(info, "DamageDistortion");
    d.thermal = num(info, "DamageThermal");
    d.biochemical = num(info, "DamageBiochemical");
    d.stun = num(info, "DamageStun");
    return d;
}

// "Item Type: Power Plant\nManufacturer: ...\n\nThe PowerBolt ...": the
// "Key: Value" header lines, and the prose after them. The text holds
// literal "\n" escapes, as base.ini does.
struct Description
{
    QHash<QString, QString> header;
    QString prose;
};

Description splitDescription(const QString &raw)
{
    Description d;
    QStringList lines = raw.split(QStringLiteral("\\n"));
    static const QRegularExpression headerLine(
        QStringLiteral("^\\s*([A-Za-z][A-Za-z /]{0,30}):\\s*(.*?)\\s*$"));
    qsizetype i = 0;
    for (; i < lines.size(); ++i) {
        const QRegularExpressionMatch m = headerLine.match(lines[i]);
        if (!m.hasMatch())
            break;
        d.header.insert(m.captured(1).trimmed(), m.captured(2));
    }
    while (i < lines.size() && lines[i].trimmed().isEmpty())
        ++i;
    d.prose = lines.mid(i).join(u'\n').trimmed();
    if (d.prose.isEmpty() && d.header.isEmpty())
        d.prose = raw.trimmed();
    return d;
}

// "COOL_AEGS_S01_Bracer_SCItem" -> "COOL AEGS S01 Bracer": for items the
// language file doesn't name.
QString fallbackName(const QString &id)
{
    QString s = id;
    static const QRegularExpression suffix(QStringLiteral("_SCItem$"),
                                           QRegularExpression::CaseInsensitiveOption);
    s.remove(suffix);
    return s.replace(u'_', u' ').simplified();
}

std::vector<LoadoutEntry> parseLoadout(Node loadoutParent, const QHash<QString, int> &byGuid,
                                       const std::vector<Item> *items)
{
    std::vector<LoadoutEntry> out;
    const Node entries = find(loadoutParent, "SItemPortLoadoutManualParams/entries");
    if (!entries)
        return out;
    for (const Node entry : children(entries)) {
        if (enh::tag(entry) != "SItemPortLoadoutEntryParams")
            continue;
        LoadoutEntry e;
        e.port = qs(getOr(entry, "itemPortName"));
        e.item = qs(getOr(entry, "entityClassName"));
        const QString ref = qs(getOr(entry, "entityClassReference"));
        if (!ref.isEmpty() && ref != qs(enh::kNullUuid)) {
            if (items) {
                const auto it = byGuid.constFind(ref);
                if (it != byGuid.cend())
                    e.refItem = (*items)[std::size_t(*it)].id;
            } else {
                e.refItem = u'#' + ref; // resolved once every item is known
            }
        }
        if (const Node sub = find(entry, "loadout"))
            e.children = parseLoadout(sub, byGuid, items);
        out.push_back(std::move(e));
    }
    return out;
}

// What a vehicle XML ItemPort or an item's SItemPortDef says.
Port portFromVehicleXml(const QString &name, Node itemPort, const missions::LocText &loc)
{
    Port p;
    p.name = name;
    p.label = loc(textAny(itemPort, {"display_name", "displayName"}));
    p.minSize = int(numAny(itemPort, {"minSize", "minsize"}));
    p.maxSize = int(numAny(itemPort, {"maxSize", "maxsize"}));
    const QStringList flags = splitTags(textAny(itemPort, {"flags", "Flags"}));
    p.editable = !flags.contains(QStringLiteral("uneditable"));
    p.hidden = flags.contains(QStringLiteral("invisible"));
    p.portTags = splitTags(textAny(itemPort, {"portTags", "PortTags"}));
    p.requiredTags = splitTags(textAny(itemPort, {"requiredTags", "RequiredTags"}));
    if (const Node types = find(itemPort, "Types")) {
        for (const Node t : children(types)) {
            if (enh::tag(t) != "Type")
                continue;
            PortType pt;
            pt.type = textAny(t, {"type", "Type"});
            pt.subTypes = splitTags(textAny(t, {"subtypes", "subTypes", "SubTypes"}));
            if (!pt.type.isEmpty())
                p.types.push_back(std::move(pt));
        }
    }
    return p;
}

Port portFromItemDef(Node def, const missions::LocText &loc)
{
    Port p;
    p.name = qs(getOr(def, "Name"));
    p.label = loc(qs(getOr(def, "DisplayName")));
    p.minSize = int(num(def, "MinSize"));
    p.maxSize = int(num(def, "MaxSize"));
    const QStringList flags = splitTags(qs(getOr(def, "Flags")));
    p.editable = !flags.contains(QStringLiteral("uneditable"));
    p.hidden = flags.contains(QStringLiteral("invisible"));
    p.portTags = splitTags(qs(getOr(def, "PortTags")));
    p.requiredTags = splitTags(qs(getOr(def, "RequiredPortTags")));
    if (const Node types = find(def, "Types")) {
        for (const Node t : children(types)) {
            if (enh::tag(t) != "SItemPortDefTypes")
                continue;
            PortType pt;
            pt.type = qs(getOr(t, "Type"));
            for (const Node e : iter(t, "Enum"))
                if (const QString v = qs(getOr(e, "value")); !v.isEmpty())
                    pt.subTypes << v;
            if (!pt.type.isEmpty())
                p.types.push_back(std::move(pt));
        }
    }
    return p;
}

// An amount in a resource delta: segments, standard units, or the smaller
// units.
double resourceAmount(Node amountParent)
{
    for (const Node n : iter(amountParent)) {
        const std::string_view t = enh::tag(n);
        if (t == "SPowerSegmentResourceUnit")
            return num(n, "units");
        if (t == "SStandardResourceUnit")
            return num(n, "standardResourceUnits");
        if (t == "SCentiResourceUnit")
            return num(n, "centiResourceUnits") / 100.0;
        if (t == "SMicroResourceUnit")
            return num(n, "microResourceUnits");
    }
    return 0;
}

Resources parseResources(Node root)
{
    Resources r;
    const Node params = findDescendant(root, "ItemResourceComponentParams");
    const Node state = params ? find(params, "states/ItemResourceState") : Node();
    if (!state)
        return r;
    if (const Node deltas = find(state, "deltas")) {
        for (const Node delta : children(deltas)) {
            const double minFraction = num(delta, "minimumConsumptionFraction");
            for (const Node part : children(delta)) {
                const std::string_view t = enh::tag(part);
                const std::string_view resource = getOr(part, "resource");
                const double amount = resourceAmount(part);
                if (t == "consumption") {
                    if (resource == "Power") {
                        r.powerDraw += amount;
                        r.powerMinFraction = minFraction;
                    } else if (resource == "QuantumFuel") {
                        r.quantumFuel += amount;
                    }
                } else if (t == "generation") {
                    if (resource == "Power")
                        r.powerOutput += amount;
                    else if (resource == "Coolant")
                        r.coolantOutput += amount;
                }
            }
        }
    }
    // A quantum drive burns fuel in its travelling state, not the first one.
    for (const Node other : findAll(params, "states/ItemResourceState")) {
        if (other == state)
            continue;
        for (const Node part : iter(other, "consumption"))
            if (getOr(part, "resource") == "QuantumFuel")
                r.quantumFuel = std::max(r.quantumFuel, resourceAmount(part));
    }
    if (const Node sig = find(state, "signatureParams")) {
        r.em = num(find(sig, "EMSignature"), "nominalSignature");
        r.ir = num(find(sig, "IRSignature"), "nominalSignature");
    }
    if (const Node ranges = find(state, "powerRanges"))
        for (const char *level : {"low", "medium", "high"})
            if (const Node n = find(ranges, level))
                r.ranges.push_back({num(n, "start"), num(n, "modifier", 1)});
    return r;
}

// The first element in `n`'s subtree (n included) whose tag contains
// `part` and that has a positive `attr`.
Node firstWith(Node n, std::string_view part, const char *attr)
{
    Node found;
    enh::forEachElement(n, [&](Node el) {
        if (enh::tag(el).find(part) == std::string_view::npos || num(el, attr) <= 0)
            return true;
        found = el;
        return false;
    });
    return found;
}

struct AmmoInfo
{
    Damage damage;
    double speed = 0, lifetime = 0, penetration = 0;
};

// Records by __ref, read on first use: ammo and manufacturers.
class RefIndex
{
public:
    void add(const QStringList &files)
    {
        static const QRegularExpression ref(QStringLiteral("__ref=\"([0-9a-f-]{36})\""));
        for (const QString &file : files) {
            QFile f(file);
            if (!f.open(QIODevice::ReadOnly))
                continue;
            const QString head = QString::fromUtf8(f.read(4096));
            if (const QRegularExpressionMatch m = ref.match(head); m.hasMatch())
                files_.insert(m.captured(1), file);
        }
    }
    QString file(const QString &guid) const { return files_.value(guid); }

private:
    QHash<QString, QString> files_;
};

class Builder
{
public:
    Builder(const CatalogSources &sources, const enh::RecordStore &store);

    void addItems(const std::atomic<bool> *cancel);
    void addShips(const std::atomic<bool> *cancel);
    Catalog take();

private:
    void addItem(const QString &file, Node root);
    std::optional<WeaponStats> weapon(Node root);
    const AmmoInfo *ammo(const QString &guid);
    std::pair<QString, QString> manufacturer(const QString &guid); // name, code
    void addShip(const QString &file, bool ground);
    void resolve(std::vector<LoadoutEntry> &entries) const;

    const CatalogSources &sources_;
    const enh::RecordStore &store_;
    missions::LocText loc_;
    RefIndex ammoFiles_, manufacturerFiles_;
    QHash<QString, AmmoInfo> ammo_;
    QHash<QString, std::pair<QString, QString>> manufacturers_;
    Catalog catalog_;
};

Builder::Builder(const CatalogSources &sources, const enh::RecordStore &store)
    : sources_(sources), store_(store), loc_(*sources.loc)
{
    ammoFiles_.add(store.rglob(QStringLiteral("ammoparams/vehicle")));
    manufacturerFiles_.add(store.rglob(QStringLiteral("scitemmanufacturer")));
}

const AmmoInfo *Builder::ammo(const QString &guid)
{
    if (const auto it = ammo_.constFind(guid); it != ammo_.cend())
        return &*it;
    const QString file = ammoFiles_.file(guid);
    if (file.isEmpty())
        return nullptr;
    const XmlDoc doc = XmlDoc::load(file);
    if (!doc)
        return nullptr;
    const Node root = doc.root();
    AmmoInfo a;
    a.speed = num(root, "speed");
    a.lifetime = num(root, "lifetime");
    const Node projectile = find(root, "projectileParams");
    if (const Node damage = projectile ? find(projectile, ".//damage") : Node())
        for (const Node info : iter(damage, "DamageInfo"))
            a.damage += damageOf(info);
    if (const Node pen = projectile ? find(projectile, ".//penetrationParams") : Node())
        a.penetration = num(pen, "basePenetrationDistance");
    return &*ammo_.insert(guid, a);
}

std::pair<QString, QString> Builder::manufacturer(const QString &guid)
{
    if (guid.isEmpty() || guid == qs(enh::kNullUuid))
        return {};
    if (const auto it = manufacturers_.constFind(guid); it != manufacturers_.cend())
        return *it;
    std::pair<QString, QString> m;
    if (const QString file = manufacturerFiles_.file(guid); !file.isEmpty())
        if (const XmlDoc doc = XmlDoc::load(file)) {
            const Node root = doc.root();
            m.second = qs(getOr(root, "Code"));
            m.first = loc_(qs(getOr(find(root, "Localization"), "Name")));
            if (m.first.isEmpty())
                m.first = m.second;
        }
    manufacturers_.insert(guid, m);
    return m;
}

std::optional<WeaponStats> Builder::weapon(Node root)
{
    const Node params = findDescendant(root, "SCItemWeaponComponentParams");
    const Node actions = params ? find(params, "fireActions") : Node();
    Node first;
    for (const Node c : children(actions)) {
        first = c;
        break;
    }
    if (!first)
        return std::nullopt;
    WeaponStats w;
    w.fireMode = qs(getOr(first, "name"));
    if (const Node fire = firstWith(first, "WeaponActionFire", "fireRate")) {
        w.fireRate = num(fire, "fireRate");
        w.heatPerShot = num(fire, "heatPerShot");
    }
    double multiplier = 1;
    for (const Node launcher : iter(first, "SProjectileLauncher")) {
        w.pellets = std::max(1, int(num(launcher, "pelletCount", 1)));
        multiplier = num(launcher, "damageMultiplier", 1);
        break;
    }
    if (const Node container = findDescendant(root, "SAmmoContainerComponentParams")) {
        w.ammo = int(num(container, "maxAmmoCount"));
        if (const AmmoInfo *a = ammo(qs(getOr(container, "ammoParamsRecord")))) {
            w.damage = a->damage * (w.pellets * multiplier);
            w.speed = a->speed;
            w.range = a->speed * a->lifetime;
            w.penetration = a->penetration;
        }
    }
    if (const Node regen = findDescendant(root, "SWeaponRegenConsumerParams")) {
        w.regenerates = true;
        w.ammo = int(num(regen, "maxAmmoLoad"));
        w.regenPerSecond = num(regen, "maxRegenPerSec");
        w.regenCooldown = num(regen, "regenerationCooldown");
    }
    if (const Node heat = findDescendant(root, "SWeaponSimplifiedHeatParams")) {
        w.overheatTemperature = num(heat, "overheatTemperature");
        w.overheatFixTime = num(heat, "overheatFixTime");
    }
    return w;
}

// The AttachDef types a loadout shows or counts; the rest (seats, doors,
// screens, controllers) are skipped before they are parsed.
const QSet<QString> &wantedTypes()
{
    static const QSet<QString> types = {
        QStringLiteral("WeaponGun"),
        QStringLiteral("WeaponDefensive"),
        QStringLiteral("WeaponMining"),
        QStringLiteral("Turret"),
        QStringLiteral("TurretBase"),
        QStringLiteral("MissileLauncher"),
        QStringLiteral("BombLauncher"),
        QStringLiteral("Missile"),
        QStringLiteral("Bomb"),
        QStringLiteral("Shield"),
        QStringLiteral("PowerPlant"),
        QStringLiteral("Cooler"),
        QStringLiteral("QuantumDrive"),
        QStringLiteral("Radar"),
        QStringLiteral("LifeSupportGenerator"),
        QStringLiteral("MainThruster"),
        QStringLiteral("ManneuverThruster"),
        QStringLiteral("FuelTank"),
        QStringLiteral("QuantumFuelTank"),
        QStringLiteral("FuelIntake"),
        QStringLiteral("Armor"),
        QStringLiteral("FlightController"),
        QStringLiteral("Paints"),
        QStringLiteral("TractorBeam"),
        QStringLiteral("SalvageHead"),
        QStringLiteral("SalvageModifier"),
        QStringLiteral("EMP"),
        QStringLiteral("QuantumInterdictionGenerator"),
        QStringLiteral("UtilityTurret"),
        QStringLiteral("ToolArm"),
        QStringLiteral("MiningModifier"),
        QStringLiteral("Module"),
        QStringLiteral("WeaponMount"),
        QStringLiteral("TowingBeam"),
    };
    return types;
}

void Builder::addItems(const std::atomic<bool> *cancel)
{
    static const QRegularExpression attachType(QStringLiteral("<AttachDef [^>]*?\\bType=\"([A-Za-z]+)\""));
    // Ship items, ground vehicle items, weapon mounts, mounted guns, and the
    // doors folder's few modules.
    QStringList files;
    for (const char *dir :
         {"entities/scitem/ships", "entities/scitem/vehicles", "entities/scitem/weaponmounts",
          "entities/scitem/weapons/weapon_mounted", "entities/scitem/doors"})
        files += store_.rglob(QLatin1StringView(dir));
    for (const QString &file : std::as_const(files)) {
        if (cancel && cancel->load())
            return;
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QByteArray bytes = f.readAll();
        const QRegularExpressionMatch m = attachType.match(QString::fromUtf8(bytes));
        if (!m.hasMatch() || !wantedTypes().contains(m.captured(1)))
            continue;
        const XmlDoc doc = XmlDoc::parse(std::string_view(bytes.constData(), std::size_t(bytes.size())));
        if (doc)
            addItem(file, doc.root());
    }
    for (std::size_t i = 0; i < catalog_.items.size(); ++i) {
        const Item &item = catalog_.items[i];
        catalog_.byId.insert(item.id.toLower(), int(i));
        if (!item.guid.isEmpty())
            catalog_.byGuid.insert(item.guid, int(i));
    }
    for (Item &item : catalog_.items)
        resolve(item.loadout);
}

void Builder::resolve(std::vector<LoadoutEntry> &entries) const
{
    for (LoadoutEntry &e : entries) {
        if (e.refItem.startsWith(u'#')) {
            const auto it = catalog_.byGuid.constFind(e.refItem.mid(1));
            e.refItem = it == catalog_.byGuid.cend() ? QString() : catalog_.items[std::size_t(*it)].id;
        }
        resolve(e.children);
    }
}

void Builder::addItem(const QString &file, Node root)
{
    const Node attach = findDescendant(root, "AttachDef");
    if (!attach)
        return;
    Item item;
    item.id = enh::recordClassName(root, enh::fileStem(file));
    item.guid = qs(getOr(root, "__ref"));
    item.type = qs(getOr(attach, "Type"));
    item.subType = qs(getOr(attach, "SubType"));
    item.size = int(num(attach, "Size"));
    item.grade = int(num(attach, "Grade"));
    item.tags = splitTags(qs(getOr(attach, "Tags")));
    item.requiredTags = splitTags(qs(getOr(attach, "RequiredTags")));
    const auto [mfr, code] = manufacturer(qs(getOr(attach, "Manufacturer")));
    item.manufacturer = mfr;
    item.manufacturerCode = code;
    if (const Node l = find(attach, "Localization")) {
        item.name = loc_(qs(getOr(l, "Name")));
        item.shortName = loc_(qs(getOr(l, "ShortName")));
        const Description d = splitDescription(loc_(qs(getOr(l, "Description"))));
        item.description = d.prose;
        item.itemClass = d.header.value(QStringLiteral("Class"));
    }
    item.named = !item.name.isEmpty();
    if (!item.named)
        item.name = fallbackName(item.id);

    enh::forEachElement(root, [&](Node el) {
        const std::string_view t = enh::tag(el);
        if (t != "SEntityRigidPhysicsControllerParams" && t != "SEntityPhysicsControllerParams")
            return true;
        if (const double m = num(el, "Mass"); m > 0) {
            item.mass = m;
            return false;
        }
        return true;
    });
    item.health = num(findDescendant(root, "SHealthComponentParams"), "Health");

    if (const Node container = findDescendant(root, "SItemPortContainerComponentParams"))
        if (const Node ports = find(container, "Ports"))
            for (const Node def : children(ports))
                if (enh::tag(def) == "SItemPortDef")
                    item.ports.push_back(portFromItemDef(def, loc_));
    if (const Node dl = findDescendant(root, "SEntityComponentDefaultLoadoutParams"))
        if (const Node l = find(dl, "loadout"))
            item.loadout = parseLoadout(l, catalog_.byGuid, nullptr);

    item.resources = parseResources(root);

    if (item.type == u"WeaponGun")
        item.weapon = weapon(root);
    if (const Node m = findDescendant(root, "SCItemMissileParams")) {
        MissileStats s;
        if (const Node e = find(m, "explosionParams"))
            s.damage = damageOf(find(e, "damage/DamageInfo"));
        if (const Node t = find(m, "targetingParams")) {
            s.tracking = qs(getOr(t, "trackingSignalType"));
            s.lockTime = num(t, "lockTime");
            s.lockAngle = num(t, "lockingAngle");
            s.lockRangeMin = num(t, "lockRangeMin");
            s.lockRangeMax = num(t, "lockRangeMax");
        }
        s.speed = num(find(m, "GCSParams"), "linearSpeed");
        s.armTime = num(m, "armTime");
        s.lifetime = num(m, "maxLifetime");
        item.missile = s;
    }
    if (const Node s = findDescendant(root, "SCItemShieldGeneratorParams")) {
        ShieldStats st;
        st.hp = num(s, "MaxShieldHealth");
        st.regen = num(s, "MaxShieldRegen");
        st.damagedDelay = num(s, "DamagedRegenDelay");
        st.downedDelay = num(s, "DownedRegenDelay");
        const auto band = [&](const char *path, Damage &lo, Damage &hi) {
            const std::vector<Node> rows = findAll(s, path);
            double *los[] = {&lo.physical, &lo.energy,      &lo.distortion,
                             &lo.thermal,  &lo.biochemical, &lo.stun};
            double *his[] = {&hi.physical, &hi.energy,      &hi.distortion,
                             &hi.thermal,  &hi.biochemical, &hi.stun};
            for (std::size_t i = 0; i < rows.size() && i < 6; ++i) {
                *los[i] = num(rows[i], "Min");
                *his[i] = num(rows[i], "Max");
            }
        };
        band("ShieldResistance/SShieldResistance", st.resistanceMin, st.resistanceMax);
        band("ShieldAbsorption/SShieldAbsorption", st.absorptionMin, st.absorptionMax);
        item.shield = st;
    }
    if (const Node q = findDescendant(root, "SCItemQuantumDriveParams")) {
        QuantumStats st;
        if (const Node p = find(q, "params")) {
            st.speed = num(p, "driveSpeed");
            st.spoolTime = num(p, "spoolUpTime");
            st.cooldown = num(p, "cooldownTime");
            st.stageOneAccel = num(p, "stageOneAccelRate");
            st.stageTwoAccel = num(p, "stageTwoAccelRate");
        }
        st.splineSpeed = num(find(q, "splineJumpParams"), "driveSpeed");
        // The travelling state's micro units read as mSCU a gigametre.
        st.fuelPerGm = item.resources.quantumFuel / 1000.0;
        item.quantum = st;
    }
    if (const Node t = findDescendant(root, "SCItemThrusterParams"))
        item.thruster = ThrusterStats{qs(getOr(t, "thrusterType")), num(t, "thrustCapacity")};
    if (const Node a = findDescendant(root, "SCItemVehicleArmorParams")) {
        ArmorStats st;
        st.emMultiplier = num(a, "signalElectromagnetic", 1);
        st.irMultiplier = num(a, "signalInfrared", 1);
        st.csMultiplier = num(a, "signalCrossSection", 1);
        if (const Node dm = find(a, "damageMultiplier/DamageInfo"))
            st.multiplier = damageOf(dm);
        if (const Node dv = find(a, "armorDeflection/deflectionValue"))
            st.deflection = damageOf(dv);
        item.armor = st;
    }
    if (const Node ifcs = findDescendant(root, "IFCSParams")) {
        FlightStats f;
        f.scmSpeed = num(ifcs, "scmSpeed");
        f.boostForward = num(ifcs, "boostSpeedForward");
        f.boostBackward = num(ifcs, "boostSpeedBackward");
        f.maxSpeed = num(ifcs, "maxSpeed");
        if (const Node av = find(ifcs, "maxAngularVelocity")) {
            f.pitch = num(av, "x");
            f.roll = num(av, "y");
            f.yaw = num(av, "z");
        }
        Node ab = find(ifcs, "afterburnerNew");
        if (!ab)
            ab = find(ifcs, "afterburner");
        if (ab) {
            if (const Node m = find(ab, "afterburnAngVelocityMultiplier")) {
                f.pitchBoost = num(m, "x", 1);
                f.rollBoost = num(m, "y", 1);
                f.yawBoost = num(m, "z", 1);
            }
            f.boostCapacity = num(ab, "capacitorMax");
            f.boostRegen = num(ab, "capacitorRegenPerSec");
            f.boostRegenDelay = num(ab, "capacitorRegenDelayAfterUse");
        }
        item.flight = f;
    }
    if (item.subType == u"CountermeasureLauncher") {
        CountermeasureStats c;
        c.ammo = int(num(findDescendant(root, "SAmmoContainerComponentParams"), "maxAmmoCount"));
        const QString hint = (qs(getOr(find(attach, "Localization"), "ShortName")) + item.id).toLower();
        c.kind = hint.contains(u"chaff") || hint.contains(u"noise") ? QStringLiteral("Noise")
                                                                    : QStringLiteral("Decoy");
        item.countermeasure = c;
    }
    if (item.type == u"FuelTank" || item.type == u"QuantumFuelTank")
        if (const Node c = findDescendant(root, "ResourceContainer"))
            item.fuelCapacity = num(find(c, "capacity/SStandardCargoUnit"), "standardCargoUnits");

    catalog_.items.push_back(std::move(item));
}

Node findById(Node root, const QString &id)
{
    Node found;
    if (id.isEmpty())
        return found;
    const QByteArray key = id.toUtf8();
    enh::forEachElement(root, [&](Node el) {
        if (getOr(el, "id") != std::string_view(key.constData(), std::size_t(key.size())))
            return true;
        found = el;
        return false;
    });
    return found;
}

// Applies a vehicle XML's <Modification name=...>: its patch file's elements
// replace the base elements with the same id, then its Elems set attributes.
// A patch can replace the root <Vehicle> itself, so the Elems are read first
// and ids are looked up afresh: a replacement frees what it replaces.
void applyModification(const XmlDoc &doc, const QString &name, const QString &vehiclesDir)
{
    if (name.isEmpty())
        return;
    Node mod;
    if (const Node mods = find(doc.root(), "Modifications"))
        for (const Node m : children(mods))
            if (qs(getOr(m, "name")).compare(name, Qt::CaseInsensitive) == 0) {
                mod = m;
                break;
            }
    if (!mod)
        return;
    struct Set
    {
        QString id;
        std::string attr, value;
    };
    std::vector<Set> sets;
    for (const Node elem : iter(mod, "Elem"))
        sets.push_back(
            {qs(getOr(elem, "idRef")), std::string(getOr(elem, "name")), std::string(getOr(elem, "value"))});
    if (const QString patch = qs(getOr(mod, "patchFile")); !patch.isEmpty()) {
        const XmlDoc patchDoc =
            XmlDoc::load(QDir(vehiclesDir).filePath(patch.toLower() + QStringLiteral(".xml")));
        if (patchDoc)
            for (const Node replacement : children(patchDoc.root())) {
                Node old = findById(doc.root(), qs(getOr(replacement, "id")));
                if (!old)
                    continue;
                Node parent = old.parent();
                parent.insert_copy_after(replacement, old);
                parent.remove_child(old);
            }
    }
    for (const Set &set : sets) {
        Node target = findById(doc.root(), set.id);
        if (!target || set.attr.empty())
            continue;
        pugi::xml_attribute a = target.attribute(set.attr.c_str());
        if (!a)
            a = target.append_attribute(set.attr.c_str());
        a.set_value(set.value.c_str());
    }
}

// A stock loadout as text, to tell copies of a ship from variants.
QString loadoutKey(const std::vector<LoadoutEntry> &entries)
{
    QStringList parts;
    for (const LoadoutEntry &e : entries)
        parts << e.port.toLower() + u'=' + e.item.toLower() + u'/' + e.refItem.toLower() + u'(' +
                     loadoutKey(e.children) + u')';
    parts.sort();
    return parts.join(u';');
}

bool skipped(Node part)
{
    const std::string_view v = getOr(part, "skipPart");
    return v == "1" || v == "true";
}

void walkParts(Node parts, int depth, Ship &ship, const missions::LocText &loc)
{
    for (const Node part : children(parts)) {
        if (enh::tag(part) != "Part" || skipped(part))
            continue;
        const QString name = qs(getOr(part, "name"));
        ship.mass += num(part, "mass");
        if (const double hp = num(part, "damageMax"); hp > 0)
            ship.hull.push_back({name, hp, depth == 1});
        if (const Node port = find(part, "ItemPort"))
            ship.ports.push_back(portFromVehicleXml(name, port, loc));
        if (const Node sub = find(part, "Parts"))
            walkParts(sub, depth + 1, ship, loc);
    }
}

void Builder::addShip(const QString &file, bool ground)
{
    const QString stem = enh::fileStem(file);
    if (!isPlayerShipRecord(stem))
        return;
    const XmlDoc doc = XmlDoc::load(file);
    if (!doc)
        return;
    const Node root = doc.root();
    const Node vpc = findDescendant(root, "VehicleComponentParams");
    if (!vpc)
        return;
    Ship ship;
    ship.id = enh::recordClassName(root, stem);
    ship.groundVehicle = ground;
    ship.name = loc_(qs(getOr(vpc, "vehicleName")));
    if (ship.name.isEmpty())
        return;
    const QString definition = qs(getOr(vpc, "vehicleDefinition")).toLower();
    constexpr QStringView xmlDir = u"scripts/entities/vehicles/implementations/xml/";
    if (!definition.startsWith(xmlDir))
        return;
    const XmlDoc vehicleDoc =
        XmlDoc::load(QDir(sources_.vehiclesDir).filePath(definition.mid(xmlDir.size())));
    if (!vehicleDoc)
        return;
    applyModification(vehicleDoc, qs(getOr(vpc, "modification")), sources_.vehiclesDir);
    const Node vehicle = vehicleDoc.root();

    ship.description = splitDescription(loc_(qs(getOr(vpc, "vehicleDescription")))).prose;
    ship.career = loc_(qs(getOr(vpc, "vehicleCareer")));
    ship.role = loc_(qs(getOr(vpc, "vehicleRole")));
    ship.crew = int(num(vpc, "crewSize"));
    if (const Node box = find(vpc, "maxBoundingBoxSize")) {
        ship.width = num(box, "x");
        ship.length = num(box, "y");
        ship.height = num(box, "z");
    }
    const auto [mfr, code] = manufacturer(qs(getOr(vpc, "manufacturer")));
    ship.manufacturer = mfr;
    ship.manufacturerCode = code;
    ship.size = int(num(vehicle, "size"));
    ship.portTags = splitTags(qs(getOr(vehicle, "itemPortTags")));
    if (const Node parts = find(vehicle, "Parts"))
        walkParts(parts, 0, ship, loc_);
    // Newer ships declare ports in the record too (components, turrets, life
    // support); a record port replaces a vehicle XML one of the same name.
    if (const Node container = findDescendant(root, "SItemPortContainerComponentParams")) {
        const QStringList recordTags = splitTags(qs(getOr(container, "PortTags")));
        for (const QString &tag : recordTags)
            if (!ship.portTags.contains(tag, Qt::CaseInsensitive))
                ship.portTags << tag;
        if (const Node ports = find(container, "Ports"))
            for (const Node def : children(ports)) {
                if (enh::tag(def) != "SItemPortDef")
                    continue;
                Port port = portFromItemDef(def, loc_);
                const auto same = std::find_if(ship.ports.begin(), ship.ports.end(), [&](const Port &p) {
                    return p.name.compare(port.name, Qt::CaseInsensitive) == 0;
                });
                if (same != ship.ports.end())
                    *same = std::move(port);
                else
                    ship.ports.push_back(std::move(port));
            }
    }
    if (const Node dl = findDescendant(root, "SEntityComponentDefaultLoadoutParams"))
        if (const Node l = find(dl, "loadout"))
            ship.loadout = parseLoadout(l, catalog_.byGuid, &catalog_.items);
    if (const Node pools = findDescendant(root, "resourceNetworkPowerPools"))
        if (const Node itemPools = find(pools, "itemPools"))
            for (const Node p : children(itemPools)) {
                const std::string_view t = enh::tag(p);
                if (t != "FixedPowerPool" && t != "DynamicPowerPool")
                    continue;
                ship.powerPools.push_back(
                    {qs(getOr(p, "itemType")), int(num(p, "poolSize")), t == "FixedPowerPool"});
            }
    catalog_.ships.push_back(std::move(ship));
}

void Builder::addShips(const std::atomic<bool> *cancel)
{
    if (!QFileInfo(sources_.vehiclesDir).isDir())
        return;
    catalog_.hasVehicles = true;
    for (const auto &[dir, ground] : {std::pair{QStringLiteral("entities/spaceships"), false},
                                      std::pair{QStringLiteral("entities/groundvehicles"), true}}) {
        const QStringList files = store_.rglob(dir);
        for (const QString &file : files) {
            if (cancel && cancel->load())
                return;
            addShip(file, ground);
        }
    }
    // Records under one name: copies with the same stock loadout go (the
    // shortest record name stays); the rest are variants and keep the part of
    // the record name that sets them apart ("Kruger S-65 Stingray (Ballistic)").
    std::stable_sort(catalog_.ships.begin(), catalog_.ships.end(), [](const Ship &a, const Ship &b) {
        if (a.name != b.name)
            return a.name.localeAwareCompare(b.name) < 0;
        return a.id.size() < b.id.size();
    });
    std::vector<Ship> kept;
    for (std::size_t i = 0; i < catalog_.ships.size();) {
        std::size_t end = i;
        while (end < catalog_.ships.size() && catalog_.ships[end].name == catalog_.ships[i].name)
            ++end;
        std::vector<Ship> group;
        QSet<QString> seen;
        for (std::size_t j = i; j < end; ++j) {
            const QString key = loadoutKey(catalog_.ships[j].loadout);
            if (seen.contains(key))
                continue;
            seen.insert(key);
            group.push_back(std::move(catalog_.ships[j]));
        }
        if (group.size() > 1) {
            qsizetype common = group.front().id.size();
            for (const Ship &s : group) {
                qsizetype n = 0;
                while (n < common && n < s.id.size() && s.id[n].toLower() == group.front().id[n].toLower())
                    ++n;
                common = n;
            }
            for (Ship &s : group) {
                const QString rest = s.id.mid(common).replace(u'_', u' ').simplified();
                if (!rest.isEmpty())
                    s.name += QStringLiteral(" (%1)").arg(rest);
            }
        }
        for (Ship &s : group)
            kept.push_back(std::move(s));
        i = end;
    }
    catalog_.ships = std::move(kept);
    std::stable_sort(catalog_.ships.begin(), catalog_.ships.end(),
                     [](const Ship &a, const Ship &b) { return a.name.localeAwareCompare(b.name) < 0; });
}

Catalog Builder::take()
{
    return std::move(catalog_);
}

bool same(const QString &a, QLatin1StringView b)
{
    return a.compare(b, Qt::CaseInsensitive) == 0;
}

// Tags compare without the "$" some records put in front of them.
QStringView bareTag(const QString &tag)
{
    return tag.startsWith(u'$') ? QStringView(tag).sliced(1) : QStringView(tag);
}

bool hasTag(const QStringList &tags, const QString &tag)
{
    const QStringView want = bareTag(tag);
    return std::any_of(tags.begin(), tags.end(),
                       [&](const QString &t) { return bareTag(t).compare(want, Qt::CaseInsensitive) == 0; });
}

bool typeMatches(const PortType &t, const Item &item)
{
    if (t.type.compare(item.type, Qt::CaseInsensitive) == 0) {
        // An item of no particular subtype goes wherever its type does.
        if (t.subTypes.isEmpty() || item.subType.isEmpty() || same(item.subType, "UNDEFINED"_L1))
            return true;
        for (const QString &s : t.subTypes)
            if (s.compare(item.subType, Qt::CaseInsensitive) == 0)
                return true;
        return false;
    }
    // Gimbal mounts go in gun ports (mining gimbals in mining laser ports and
    // the other way round), and a bare turret arm takes the guns and tools
    // made for mounts, as the stock loadouts have them.
    if (same(t.type, "WeaponGun"_L1) && same(item.type, "Turret"_L1) && same(item.subType, "GunTurret"_L1))
        return true;
    if ((same(t.type, "WeaponMining"_L1) && same(item.type, "UtilityTurret"_L1)) ||
        (same(t.type, "UtilityTurret"_L1) && same(item.type, "WeaponMining"_L1)))
        return true;
    return same(t.type, "Turret"_L1) && t.subTypes.isEmpty() &&
           hasTag(item.tags, QStringLiteral("weaponMountUsable"));
}

} // namespace

double WeaponStats::timeToOverheat() const
{
    const double heatPerSecond = heatPerShot * fireRate / 60.0;
    return heatPerSecond > 0 && overheatTemperature > 0 ? overheatTemperature / heatPerSecond : 0;
}

double WeaponStats::sustainedDps(double efficiency) const
{
    const double burst = burstDps();
    if (burst <= 0)
        return 0;
    double sustained = burst;
    if (const double fire = timeToOverheat(); fire > 0)
        sustained = std::min(sustained, burst * fire / (fire + overheatFixTime));
    if (regenerates && ammo > 0) {
        const double empty = ammo / (fireRate / 60.0);
        const double regen = regenPerSecond * std::clamp(efficiency, 0.0, 1.0);
        const double refill = regen > 0 ? regenCooldown + ammo / regen : 0;
        sustained = regen > 0 ? std::min(sustained, ammo * alpha() / (empty + refill)) : 0;
    }
    return sustained;
}

QString Item::gradeLetter() const
{
    return grade >= 1 && grade <= 26 ? QString(QChar(u'A' + grade - 1)) : QString();
}

double Ship::hullHp() const
{
    double hp = 0;
    for (const HullPart &p : hull)
        hp += p.hp;
    return hp;
}

double Ship::vitalHp() const
{
    double hp = 0;
    for (const HullPart &p : hull)
        if (p.vital)
            hp += p.hp;
    return hp;
}

const Item *Catalog::item(const QString &id) const
{
    const auto it = byId.constFind(id.toLower());
    return it == byId.cend() ? nullptr : &items[std::size_t(*it)];
}

const Ship *Catalog::ship(const QString &id) const
{
    for (const Ship &s : ships)
        if (s.id.compare(id, Qt::CaseInsensitive) == 0)
            return &s;
    return nullptr;
}

bool isPlayerShipRecord(const QString &recordName)
{
    static const QRegularExpression notPlayer(
        QStringLiteral("(^|_)(ai|pu|ea|template|tutorial|teach|fleetweek|fw\\d*\\w*|temp|tier|unmanned|"
                       "shipboarded|swarm|test|override|bis\\d+\\w*|dead|lowfuel|nointerior|prison|hijacked|"
                       "derelict|boarded|wreck|indestructible|advocacy|mission|drug|piano|plat)(_|$)|"
                       "^(probe|spaceship|eaobjective|orbital_sentry)"),
        QRegularExpression::CaseInsensitiveOption);
    return !notPlayer.match(recordName).hasMatch();
}

bool fits(const Port &port, const Item &item, const QStringList &shipTags)
{
    if (port.maxSize > 0 && (item.size < port.minSize || item.size > port.maxSize))
        return false;
    if (!port.types.empty() && std::none_of(port.types.begin(), port.types.end(),
                                            [&](const PortType &t) { return typeMatches(t, item); }))
        return false;
    for (const QString &tag : item.requiredTags)
        if (!hasTag(port.portTags, tag) && !hasTag(shipTags, tag))
            return false;
    for (const QString &tag : port.requiredTags)
        if (!hasTag(item.tags, tag))
            return false;
    return true;
}

Catalog buildCatalog(const CatalogSources &sources, const std::atomic<bool> *cancel)
{
    static const IniMap empty;
    CatalogSources s = sources;
    if (!s.loc)
        s.loc = &empty;
    const auto store = enh::RecordStore::scan(s.recordsDir);
    Builder builder(s, *store);
    builder.addItems(cancel);
    builder.addShips(cancel);
    return builder.take();
}

} // namespace core::loadout
