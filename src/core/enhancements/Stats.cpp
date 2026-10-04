#include "core/enhancements/Stats.h"

#include "core/text/PyFormat.h"
#include "core/text/PyText.h"

#include <QMap>

#include <algorithm>
#include <map>

namespace core::enh {

namespace {

constexpr QLatin1StringView kPipe("  |  ");

QString joinLines(const QStringList &lines)
{
    return lines.join(kNl);
}

bool isZeroText(const std::optional<std::string_view> &v)
{
    return v && (*v == "0" || *v == "0.0");
}

// `v not in (None, "0", "0.0")`
bool setAndNonZero(const std::optional<std::string_view> &v)
{
    return v && !isZeroText(v);
}

std::optional<std::string_view> attrIn(Node root, std::string_view tagName, const char *attr)
{
    return attrOf(root, tagName, attr);
}

QString lower(std::string_view s)
{
    return qs(s).toLower();
}

// f"{v:,.0f}", f"{v:.2f}" and friends.
QString f0(double v)
{
    return py::fixed(v, 0, true);
}

QString rangeM(double v)
{
    return v >= 1000 ? py::fixed(v / 1000, 1, true) + QStringLiteral(" km") : py::fixed(v, 0, true) + QStringLiteral(" m");
}

void addSignatures(QStringList &lines, const std::optional<std::string_view> &em,
                   const std::optional<std::string_view> &ir)
{
    if (!em && !ir)
        return;
    QStringList parts;
    if (em)
        parts << QStringLiteral("EM: ") + fmt(em);
    if (ir)
        parts << QStringLiteral("IR: ") + fmt(ir);
    lines << QStringLiteral("Signatures:  ") + parts.join(kPipe);
}

void addOverheat(QStringList &lines, const std::optional<std::string_view> &overheat)
{
    if (!overheat)
        return;
    const std::optional<double> v = toFloat(overheat);
    if (!v || *v < kOverheatPlaceholder)
        lines << QStringLiteral("Overheat Temp: ") + fmt(overheat, u"K");
}

QString damageTypeSuffix(const DamageBreakdown &b)
{
    if (b.parts.size() == 1)
        return QStringLiteral(" (%1)").arg(b.parts.front().first);
    if (b.parts.size() > 1) {
        QStringList parts;
        for (const auto &[label, v] : b.parts)
            parts << label + QStringLiteral(": ") + py::fixed(v, 1);
        return QStringLiteral(" (") + parts.join(QStringLiteral(" / ")) + u')';
    }
    return QString();
}

constexpr std::array kDamageTypes = {"DamagePhysical", "DamageEnergy",      "DamageDistortion",
                                     "DamageThermal",  "DamageBiochemical", "DamageStun"};

QString damageLabel(const char *attr)
{
    static const QHash<QString, QString> labels = {
        {QStringLiteral("DamagePhysical"), QStringLiteral("Phys")},
        {QStringLiteral("DamageEnergy"), QStringLiteral("Energy")},
        {QStringLiteral("DamageDistortion"), QStringLiteral("Distort")},
        {QStringLiteral("DamageThermal"), QStringLiteral("Thermal")},
        {QStringLiteral("DamageBiochemical"), QStringLiteral("Bio")},
        {QStringLiteral("DamageStun"), QStringLiteral("Stun")},
    };
    return labels.value(QString::fromLatin1(attr));
}

// float(info.get(attr, 0)): a missing attribute is 0; nothing for junk.
std::optional<double> damageValue(Node info, const char *attr)
{
    const auto v = get(info, attr);
    return v ? toFloat(*v) : std::optional(0.0);
}

void addDamage(DamageBreakdown &b, Node info)
{
    for (const char *attr : kDamageTypes) {
        const std::optional<double> v = damageValue(info, attr);
        if (!v || *v == 0)
            continue;
        const QString label = damageLabel(attr);
        auto it = std::find_if(b.parts.begin(), b.parts.end(), [&](const auto &p) { return p.first == label; });
        if (it == b.parts.end())
            b.parts.emplace_back(label, *v);
        else
            it->second += *v;
    }
}

} // namespace

double requireFloat(const std::optional<std::string_view> &value)
{
    if (!value)
        PyError::raise("float() argument must be a string or a real number, not 'NoneType'");
    const std::optional<double> v = toFloat(*value);
    if (!v)
        PyError::raise("could not convert string to float");
    return *v;
}

std::optional<std::string_view> firstSet(Node el, std::initializer_list<const char *> names)
{
    for (const char *name : names)
        if (const auto v = get(el, name); v && !v->empty())
            return v;
    return std::nullopt;
}

std::optional<std::string_view> resourceAmount(Node amount)
{
    if (const Node unit = find(amount, ".//SPowerSegmentResourceUnit"))
        return get(unit, "units");
    if (const Node std_ = find(amount, ".//SStandardResourceUnit"))
        return get(std_, "standardResourceUnits");
    if (const Node micro = find(amount, ".//SMicroResourceUnit"))
        return get(micro, "microResourceUnits");
    return std::nullopt;
}

std::optional<std::string_view> findResource(Node root, std::string_view resource)
{
    for (const char *deltaType :
         {"ItemResourceDeltaGeneration", "ItemResourceDeltaConversion", "ItemResourceDeltaConsumption"}) {
        for (const Node delta : iter(root, deltaType))
            for (const Node child : children(delta))
                if (get(child, "resource") == resource)
                    if (const auto v = resourceAmount(child))
                        return v;
    }
    return std::nullopt;
}

std::optional<double> fireRate(Node root)
{
    struct Rate
    {
        double value;
        bool primary;
    };
    std::vector<Rate> rates;
    forEachElement(root, [&](Node el) {
        if (tag(el).find("WeaponActionFire") == std::string_view::npos)
            return true;
        const std::string_view fr = getOr(el, "fireRate");
        if (fr.empty())
            return true;
        const std::optional<double> v = toFloat(fr);
        if (!v || *v <= 0)
            return true;
        const bool isDefault = getOr(el, "default") == "1" || getOr(el, "isDefault") == "true";
        const bool primary = isDefault || lower(getOr(el, "actionType")).contains(u"primary");
        rates.push_back({*v, primary});
        return true;
    });
    if (rates.empty())
        return std::nullopt;
    std::stable_sort(rates.begin(), rates.end(), [](Rate a, Rate b) {
        if (a.primary != b.primary)
            return a.primary;
        return a.value > b.value;
    });
    return rates.front().value;
}

QStringList fireModes(Node root, const Loc *loc)
{
    static const QHash<QString, QString> labels = {
        {QStringLiteral("rapid"), QStringLiteral("Auto")},       {QStringLiteral("single"), QStringLiteral("Semi-Auto")},
        {QStringLiteral("burst"), QStringLiteral("Burst")},      {QStringLiteral("charge"), QStringLiteral("Charge")},
        {QStringLiteral("shotgun"), QStringLiteral("Shotgun")},
    };
    const auto stripBrackets = [](QString s) {
        qsizetype b = 0;
        qsizetype e = s.size();
        const auto junk = [](QChar c) { return c == u'[' || c == u']' || c == u' '; };
        while (b < e && junk(s[b]))
            ++b;
        while (e > b && junk(s[e - 1]))
            --e;
        return s.sliced(b, e - b);
    };
    QStringList names;
    forEachElement(root, [&](Node el) {
        if (tag(el).find("WeaponActionFire") == std::string_view::npos)
            return true;
        const QString rawName = py::strip(qs(getOr(el, "name")));
        QString label = labels.value(rawName.toLower());
        if (label.isEmpty()) {
            const std::string_view locRef = getOr(el, "localisedName");
            if (isSentinelLocRef(locRef))
                return true;
            if (locRef.starts_with('@') && loc) {
                QString resolved = loc->value(qs(locRef.substr(1)));
                if (resolved.isEmpty())
                    resolved = rawName;
                label = stripBrackets(resolved);
            } else {
                label = rawName.isEmpty() ? stripBrackets(qs(locRef)) : rawName;
            }
        }
        if (!label.isEmpty() && !isPlaceholderText(label) && !names.contains(label))
            names << label;
        return true;
    });
    return names;
}

DamageBreakdown ammoDamageBreakdown(Node ammoRoot)
{
    DamageBreakdown b;
    if (const Node damage = find(ammoRoot, ".//damage")) {
        for (const Node info : iter(damage, "DamageInfo"))
            addDamage(b, info);
    } else {
        // The first DamageInfo anywhere.
        const std::vector<Node> infos = iter(ammoRoot, "DamageInfo");
        if (!infos.empty())
            addDamage(b, infos.front());
    }
    for (const auto &p : b.parts)
        b.total += p.second;
    return b;
}

QString enhancementsShield(Node root)
{
    const Node el = findDescendant(root, "SCItemShieldGeneratorParams");
    if (!el)
        return QString();
    const auto hp = get(el, "MaxShieldHealth");
    const auto regen = get(el, "MaxShieldRegen");
    const auto downed = get(el, "DownedRegenDelay");
    const auto damaged = get(el, "DamagedRegenDelay");
    const auto pwr = findResource(root, "Power");
    const auto compHp = attrIn(root, "SHealthComponentParams", "Health");
    const auto em = attrIn(root, "EMSignature", "nominalSignature");
    const auto ir = attrIn(root, "IRSignature", "nominalSignature");
    const std::vector<Node> resist = findAll(el, "ShieldResistance/SShieldResistance");

    const auto resistPct = [&](std::size_t idx) -> QString {
        if (idx >= resist.size())
            return QString();
        const std::optional<double> mn = toFloat(getOr(resist[idx], "Min", "0"));
        const std::optional<double> mx = toFloat(getOr(resist[idx], "Max", "0"));
        if (!mn || !mx)
            return QString();
        const double lo100 = *mn * 100;
        const double hi100 = *mx * 100;
        if (lo100 == 0 && hi100 == 0)
            return QString();
        const double lo = lo100 <= hi100 ? lo100 : hi100;
        const double hi = lo100 <= hi100 ? hi100 : lo100;
        return py::fixed(lo, 0, false, true) + QStringLiteral("% – ") + py::fixed(hi, 0, false, true) + u'%';
    };

    QStringList lines;
    if (hp || regen)
        lines << QStringLiteral("Max HP: %1  |  Regen: %2").arg(fmt(hp), fmt(regen, u" HP/s"));
    QStringList delays;
    if (downed)
        delays << QStringLiteral("Downed Delay: ") + fmt(downed, u"s", 1);
    if (damaged)
        delays << QStringLiteral("Damaged Delay: ") + fmt(damaged, u"s", 1);
    if (!delays.isEmpty())
        lines << delays.join(kPipe);

    const QString phys = resistPct(0);
    const QString energy = resistPct(1);
    if (!phys.isEmpty() || !energy.isEmpty()) {
        QStringList parts;
        if (!phys.isEmpty())
            parts << QStringLiteral("Phys: ") + phys;
        if (!energy.isEmpty())
            parts << QStringLiteral("Energy: ") + energy;
        lines << QStringLiteral("Resist:  ") + parts.join(kPipe);
    }
    addSignatures(lines, em, ir);
    if (pwr)
        lines << QStringLiteral("Power Draw: ") + fmt(pwr, u" PU/s");
    if (compHp)
        lines << QStringLiteral("Component HP: ") + fmt(compHp);
    return joinLines(lines);
}

QString enhancementsMissile(Node root)
{
    QStringList lines;
    // Positive float of a truthy, non-"0" attribute value.
    const auto positive = [](const std::optional<std::string_view> &v) -> std::optional<double> {
        if (!v || v->empty() || *v == "0")
            return std::nullopt;
        const std::optional<double> f = toFloat(*v);
        return f && *f > 0 ? f : std::nullopt;
    };
    const auto noNone = [](const std::optional<std::string_view> &v) {
        return v && !v->empty() && !lower(*v).contains(u"none");
    };

    forEachElement(root, [&](Node el) {
        const QString t = lower(tag(el));
        if (t.contains(u"missile") || t.contains(u"projectile")) {
            if (const auto v = positive(firstSet(el, {"speed", "velocity", "initialVelocity"})))
                lines << QStringLiteral("Velocity: %1 m/s").arg(f0(*v));
            if (const auto v = positive(firstSet(el, {"lifetime", "maxLifetime", "burnTime"})))
                lines << QStringLiteral("Lifetime: %1s").arg(py::fixed(*v, 2));
        }
        if (t.contains(u"guidance") || t.contains(u"tracking")) {
            QString guidance;
            if (const auto g = firstSet(el, {"guidanceType", "type"}))
                guidance = qs(*g);
            else
                guidance = qs(tag(el)).replace(QStringLiteral("Guidance"), QString()).replace(QStringLiteral("Tracking"), QString());
            if (!guidance.isEmpty() && !guidance.contains(u"none", Qt::CaseInsensitive))
                lines << QStringLiteral("Guidance: ") + guidance;
            if (const auto seeker = firstSet(el, {"seekerType", "seekerMode"}); noNone(seeker))
                lines << QStringLiteral("Seeker: ") + qs(*seeker);
            if (const auto v = positive(firstSet(el, {"lockTime", "lockOnTime", "lockAcquisitionTime"})))
                lines << QStringLiteral("Lock Time: %1s").arg(py::fixed(*v, 2));
            if (const auto raw = firstSet(el, {"minLockRange", "minimumLockRange"}); raw && *raw != "0")
                if (const auto v = toFloat(*raw); v && *v / 1000 > 0)
                    lines << QStringLiteral("Min Lock Range: %1 km").arg(py::fixed(*v / 1000, 1, true));
            if (const auto raw = firstSet(el, {"maxLockRange", "lockOnRange", "launchRange"}); raw && *raw != "0")
                if (const auto v = toFloat(*raw); v && *v / 1000 > 0)
                    lines << QStringLiteral("Max Lock Range: %1 km").arg(py::fixed(*v / 1000, 1, true));
            if (const auto raw = firstSet(el, {"trackingRange", "engagementRange", "maxEngagementRange"}); raw && *raw != "0")
                if (const auto v = toFloat(*raw); v && *v / 1000 > 0)
                    lines << QStringLiteral("Tracking Range: %1 km").arg(py::fixed(*v / 1000, 1, true));
            if (const auto v = positive(firstSet(el, {"proximityFuseRange", "detonationRange", "fuseRange"})))
                lines << QStringLiteral("Proximity Range: %1 m").arg(f0(*v));
            if (const auto v = positive(firstSet(el, {"maxGForce", "maxAcceleration", "maxG"})))
                lines << QStringLiteral("Max G-Force: %1G").arg(py::fixed(*v, 1));
            if (const auto v = positive(firstSet(el, {"turnRate", "maxTurnRate", "angularVelocity"})))
                lines << QStringLiteral("Turn Rate: %1°/s").arg(py::fixed(*v, 1));
            if (const auto d = firstSet(el, {"detonationMode", "fuseMode", "detonationType"}); noNone(d))
                lines << QStringLiteral("Detonation: ") + qs(*d);
        }
        if (t.contains(u"propulsion") || t.contains(u"thruster") || t.contains(u"engine")) {
            if (const auto v = positive(firstSet(el, {"acceleration", "maxAcceleration", "thrust"})))
                lines << QStringLiteral("Acceleration: %1 m/s²").arg(py::fixed(*v, 1, true));
        }
        if (t.contains(u"propellant") || t.contains(u"fuel")) {
            if (const auto v = positive(firstSet(el, {"amount", "fuelAmount"})))
                lines << QStringLiteral("Fuel: %1s").arg(py::fixed(*v, 1));
        }
        return true;
    });

    // Lock range from targetingParams' lockRangeMin / lockRangeMax.
    const auto optFloat = [](const std::optional<std::string_view> &v) -> std::optional<double> {
        return v && !v->empty() ? toFloat(*v) : std::nullopt;
    };
    const std::optional<double> lmn = optFloat(attrIn(root, "targetingParams", "lockRangeMin"));
    const std::optional<double> lmx = optFloat(attrIn(root, "targetingParams", "lockRangeMax"));
    if (lmn && *lmn > 0 && lmx && *lmx > 0)
        lines << QStringLiteral("Lock Range: %1 – %2").arg(rangeM(*lmn), rangeM(*lmx));
    else if (lmn && *lmn > 0)
        lines << QStringLiteral("Min Lock Range: ") + rangeM(*lmn);
    else if (lmx && *lmx > 0)
        lines << QStringLiteral("Max Lock Range: ") + rangeM(*lmx);

    const std::optional<double> armT = optFloat(attrIn(root, "SCItemMissileParams", "armTime"));
    const std::optional<double> safety = optFloat(attrIn(root, "SCItemMissileParams", "explosionSafetyDistance"));
    const std::optional<double> speed = optFloat(attrIn(root, "GCSParams", "linearSpeed"));
    QStringList arm;
    if (armT && *armT != 0 && *armT > 0)
        arm << QStringLiteral("Arm Time: %1s").arg(py::fixed(*armT, 1));
    if (armT && *armT > 0 && speed && *speed > 0)
        arm << QStringLiteral("Arm Dist: ") + rangeM(*armT * *speed);
    if (safety && *safety > 0)
        arm << QStringLiteral("Min Detonate: %1 m").arg(f0(*safety));
    if (!arm.isEmpty())
        lines << arm.join(kPipe);

    if (findDescendant(root, "DamageInfo")) {
        const DamageBreakdown b = ammoDamageBreakdown(root);
        if (b.total > 0)
            lines << QStringLiteral("Damage: ") + fmt(b.total, {}, 1) + damageTypeSuffix(b);
    }

    std::optional<std::string_view> blast = attrIn(root, "ExplosionParams", "maxRadius");
    if (!blast || blast->empty())
        blast = attrIn(root, "ExplosionParams", "minRadius");
    if (!blast || blast->empty())
        blast = attrIn(root, "Warhead", "blastRadius");
    if (!blast || blast->empty())
        blast = attrIn(root, "DamageInfo", "DamageDropOffEnd");
    if (blast && !blast->empty())
        if (const auto v = toFloat(*blast); v && *v > 0)
            lines << QStringLiteral("Blast Radius: %1 m").arg(f0(*v));

    if (const auto eff = attrIn(root, "ProjectileParams", "effectiveRange"); eff && !eff->empty() && *eff != "0")
        if (const auto v = toFloat(*eff); v && *v / 1000 > 0)
            lines << QStringLiteral("Effective Range: %1 km").arg(py::fixed(*v / 1000, 1, true));
    if (const auto em = attrIn(root, "EMSignature", "nominalSignature"); em && !em->empty() && *em != "0")
        if (const auto v = toFloat(*em); v && *v > 0)
            lines << QStringLiteral("EM Signature: ") + f0(*v);
    if (const auto ir = attrIn(root, "IRSignature", "nominalSignature"); ir && !ir->empty() && *ir != "0")
        if (const auto v = toFloat(*ir); v && *v > 0)
            lines << QStringLiteral("IR Signature: ") + f0(*v);
    if (const auto hp = attrIn(root, "SHealthComponentParams", "Health"))
        lines << QStringLiteral("Component HP: ") + fmt(hp);
    return joinLines(lines);
}

QString enhancementsBombRack(Node root)
{
    const Node attach = findDescendant(root, "SAttachableComponentParams");
    if (!attach)
        return QString();
    const Node ad = find(attach, "AttachDef");
    if (!ad)
        return QString();
    const std::string_view size = getOr(ad, "Size");
    const std::string_view grade = getOr(ad, "Grade");
    std::size_t slotCount = 0;
    if (const Node rack = findDescendant(root, "SCItemMissileRackParams"))
        if (const Node slotTags = find(rack, "slotTags"))
            slotCount = findAll(slotTags, "String").size();
    const auto hp = attrIn(root, "SHealthComponentParams", "Health");
    QStringList lines;
    if (!size.empty())
        lines << QStringLiteral("Size: S") + qs(size);
    if (!grade.empty())
        lines << QStringLiteral("Grade: ") + qs(grade);
    if (slotCount > 0)
        lines << QStringLiteral("Bomb Slots: %1").arg(slotCount);
    if (hp && !hp->empty())
        lines << QStringLiteral("Component HP: ") + fmt(hp);
    return joinLines(lines);
}

QString enhancementsRadar(Node root)
{
    QStringList lines;
    if (const std::vector<Node> aim = iter(root, "aimAssist"); !aim.empty()) {
        const auto minD = get(aim.front(), "distanceMinAssignment");
        const auto maxD = get(aim.front(), "distanceMaxAssignment");
        const std::optional<double> minV = minD && !minD->empty() ? toFloat(*minD) : std::nullopt;
        const std::optional<double> maxV = maxD && !maxD->empty() ? toFloat(*maxD) : std::nullopt;
        // Both must parse: one bad number voids the line, as the shared try block does.
        if (minV && maxV && *maxV > 0)
            lines << QStringLiteral("Aim Assist Range: %1–%2 m").arg(f0(*minV), f0(*maxV));
    }
    if (const std::vector<Node> ping = iter(root, "pingProperties"); !ping.empty()) {
        if (const std::string_view cd = getOr(ping.front(), "cooldownTime"); !cd.empty())
            if (const auto v = toFloat(cd))
                lines << QStringLiteral("Ping Cooldown: %1s").arg(py::fixed(*v, 1));
    }
    bool passive = false;
    bool active = false;
    for (const Node el : iter(root, "SCItemRadarSignatureDetection")) {
        passive = passive || getOr(el, "permitPassiveDetection") == "1";
        active = active || getOr(el, "permitActiveDetection") == "1";
    }
    QStringList modes;
    if (passive)
        modes << QStringLiteral("Passive");
    if (active)
        modes << QStringLiteral("Active");
    if (!modes.isEmpty())
        lines << QStringLiteral("Detection Mode: ") + modes.join(QStringLiteral(" / "));
    if (const auto pwr = findResource(root, "Power"))
        lines << QStringLiteral("Power Draw: ") + fmt(pwr, u" PU/s");
    if (const auto hp = attrIn(root, "SHealthComponentParams", "Health"))
        lines << QStringLiteral("Component HP: ") + fmt(hp);
    return joinLines(lines);
}

QString enhancementsCooler(Node root)
{
    const auto cooling = findResource(root, "Coolant");
    const auto pwr = findResource(root, "Power");
    const auto hp = attrIn(root, "SHealthComponentParams", "Health");
    const auto em = attrIn(root, "EMSignature", "nominalSignature");
    const auto ir = attrIn(root, "IRSignature", "nominalSignature");
    const auto overheat = attrIn(root, "itemResourceParams", "overheatTemperature");
    QStringList lines;
    if (cooling)
        lines << QStringLiteral("Cooling Rate: ") + fmt(cooling, u" CR/s");
    if (pwr)
        lines << QStringLiteral("Power Draw: ") + fmt(pwr, u" PU/s");
    if (hp)
        lines << QStringLiteral("Component HP: ") + fmt(hp);
    addSignatures(lines, em, ir);
    addOverheat(lines, overheat);
    return joinLines(lines);
}

QString enhancementsPowerplant(Node root)
{
    const auto gen = findResource(root, "Power");
    const auto hp = attrIn(root, "SHealthComponentParams", "Health");
    const auto em = attrIn(root, "EMSignature", "nominalSignature");
    const auto ir = attrIn(root, "IRSignature", "nominalSignature");
    const auto overheat = attrIn(root, "itemResourceParams", "overheatTemperature");
    const auto distort = attrIn(root, "SDistortionParams", "Maximum");
    QStringList lines;
    if (gen)
        lines << QStringLiteral("Power Output: ") + fmt(gen, u" PU/s");
    if (hp)
        lines << QStringLiteral("Component HP: ") + fmt(hp);
    addSignatures(lines, em, ir);
    addOverheat(lines, overheat);
    if (distort)
        lines << QStringLiteral("Max Distortion: ") + fmt(distort);
    return joinLines(lines);
}

QString enhancementsQuantumDrive(Node root)
{
    const Node qd = findDescendant(root, "SCItemQuantumDriveParams");
    if (!qd)
        return QString();
    const auto fuelReq = get(qd, "quantumFuelRequirement");
    // The 4.x rework dropped the __type marker: try the typed form, then
    // the bare <params> child.
    Node params = findByType(root, "SQuantumDriveParams");
    if (!params)
        params = find(qd, "params");
    const auto p = [&](const char *name) { return params ? get(params, name) : std::nullopt; };
    const auto speed = p("driveSpeed");
    const auto spool = p("spoolUpTime");
    const auto cooldown = p("cooldownTime");
    const auto calRate = p("calibrationRate");
    const auto calMin = p("minCalibrationRequirement");
    const auto calMax = p("maxCalibrationRequirement");
    const auto accel1 = p("stageOneAccelRate");
    const auto accel2 = p("stageTwoAccelRate");
    const auto pwr = findResource(root, "Power");
    const auto qtFuel = findResource(root, "QuantumFuel");
    const auto hp = attrIn(root, "SHealthComponentParams", "Health");
    const auto em = attrIn(root, "EMSignature", "nominalSignature");
    const auto ir = attrIn(root, "IRSignature", "nominalSignature");
    const auto overheat = attrIn(root, "itemResourceParams", "overheatTemperature");
    const auto distort = attrIn(root, "SDistortionParams", "Maximum");

    QStringList lines;
    if (speed) {
        const double mm = requireFloat(speed) / 1'000'000;
        const QString spoolStr = spool && !spool->empty() ? fmt(spool, u"s") : QStringLiteral("?");
        lines << QStringLiteral("QT Speed: %1 Mm/s  |  Spool: %2").arg(py::fixed(mm, 0, true), spoolStr);
    }
    if (cooldown)
        lines << QStringLiteral("Cooldown: ") + fmt(cooldown, u"s", 1);
    if (fuelReq)
        lines << QStringLiteral("Fuel/Gm: ") + py::fixed(requireFloat(fuelReq), 4);
    if (qtFuel)
        lines << QStringLiteral("QT Fuel Use: %1 μ/s").arg(fmt(qtFuel));
    if (accel1 || accel2) {
        QStringList parts;
        if (accel1 && !accel1->empty())
            parts << QStringLiteral("S1: ") + fmt(accel1);
        if (accel2 && !accel2->empty())
            parts << QStringLiteral("S2: ") + fmt(accel2);
        lines << QStringLiteral("Accel:  ") + parts.join(kPipe);
    }
    if (calRate)
        lines << QStringLiteral("Cal Rate: %1  |  Required: %2–%3").arg(fmt(calRate), fmt(calMin), fmt(calMax));
    if (pwr)
        lines << QStringLiteral("Power Draw: ") + fmt(pwr, u" PU/s");
    if (hp)
        lines << QStringLiteral("Component HP: ") + fmt(hp);
    addSignatures(lines, em, ir);
    addOverheat(lines, overheat);
    if (distort)
        lines << QStringLiteral("Max Distortion: ") + fmt(distort);
    return joinLines(lines);
}

namespace {

// The beam parts shared by mining lasers and salvage tools.
void energyHeatWear(QStringList &parts, Node fa)
{
    const auto eMin = get(fa, "minEnergyDraw");
    const auto eMax = get(fa, "maxEnergyDraw");
    const auto heat = get(fa, "heatPerSecond");
    const auto wear = get(fa, "wearPerSecond");
    if (setAndNonZero(eMax)) {
        if (eMin && !eMin->empty() && *eMin != *eMax && !isZeroText(eMin))
            parts << QStringLiteral("Energy: %1–%2 PU/s").arg(fmt(eMin), fmt(eMax));
        else
            parts << QStringLiteral("Energy: %1 PU/s").arg(fmt(eMax));
    }
    if (setAndNonZero(heat))
        parts << QStringLiteral("Heat: %1/s").arg(fmt(heat));
    if (setAndNonZero(wear))
        parts << QStringLiteral("Wear: %1/s").arg(qs(*wear));
}

QString signedPercent(const QString &label, std::string_view value)
{
    const QString sign = requireFloat(value) > 0 ? QStringLiteral("+") : QString();
    return QStringLiteral("%1: %2%3%").arg(label, sign, qs(value));
}

} // namespace

QString enhancementsMiningLaser(Node root)
{
    QStringList lines;
    std::vector<Node> actions = findAll(root, ".//fireActions/SWeaponActionFireBeamParams");
    if (actions.empty())
        actions = findAll(root, ".//SWeaponActionFireBeamParams");
    int idx = 0;
    for (const Node fa : actions) {
        ++idx;
        const Node mannequin = find(fa, "mannequinTag");
        const std::string_view mtag = mannequin ? getOr(mannequin, "tag") : std::string_view();
        const QString mode = mtag == "laser"     ? QStringLiteral("Fracture")
                             : mtag == "tractor" ? QStringLiteral("Extraction")
                                                 : QStringLiteral("Beam %1").arg(idx);
        const Node dpsEl = find(fa, "damagePerSecond/DamageInfo");
        const auto dps = dpsEl ? get(dpsEl, "DamageEnergy") : std::nullopt;
        const auto fullR = get(fa, "fullDamageRange");
        const auto zeroR = get(fa, "zeroDamageRange");
        bool nonzero = false;
        for (const auto &v : {dps, fullR, zeroR, get(fa, "minEnergyDraw"), get(fa, "maxEnergyDraw"),
                              get(fa, "heatPerSecond"), get(fa, "wearPerSecond")})
            nonzero = nonzero || (v && !v->empty() && !isZeroText(v));
        if (!nonzero)
            continue;
        QStringList parts;
        if (setAndNonZero(dps))
            parts << QStringLiteral("DPS: ") + fmt(dps);
        if (setAndNonZero(fullR) || setAndNonZero(zeroR))
            parts << QStringLiteral("Range: %1–%2").arg(fmt(fullR, u"m"), fmt(zeroR, u"m"));
        energyHeatWear(parts, fa);
        if (!parts.isEmpty())
            lines << mode + QStringLiteral(":  ") + parts.join(kPipe);
    }

    if (const Node mlp = findDescendant(root, "SEntityComponentMiningLaserParams")) {
        if (const Node modifiers = find(mlp, "miningLaserModifiers")) {
            QStringList mods;
            for (const auto &[child, label] : {std::pair{"laserInstability", "Instability"},
                                               {"optimalChargeWindowSizeModifier", "Optimal Charge Window"},
                                               {"resistanceModifier", "Resistance"}}) {
                const Node fm = find(modifiers, std::string(child) + "/FloatModifierMultiplicative");
                if (!fm)
                    continue;
                const auto val = get(fm, "value");
                if (val && !val->empty() && !isZeroText(val))
                    mods << signedPercent(QString::fromLatin1(label), *val);
            }
            if (const Node filter = find(mlp, "filterParams/filterModifier/FloatModifierMultiplicative")) {
                const auto val = get(filter, "value");
                if (val && !val->empty() && !isZeroText(val))
                    mods << signedPercent(QStringLiteral("Inert Filter"), *val);
            }
            if (!mods.isEmpty())
                lines << QStringLiteral("Modifiers:  ") + mods.join(kPipe);
        }
    }

    if (const auto hp = attrIn(root, "SHealthComponentParams", "Health"))
        lines << QStringLiteral("Component HP: ") + fmt(hp);
    if (const auto distort = attrIn(root, "SDistortionParams", "Maximum"); setAndNonZero(distort))
        lines << QStringLiteral("Max Distortion: ") + fmt(distort);
    const auto em = attrIn(root, "EMSignature", "nominalSignature");
    const auto ir = attrIn(root, "IRSignature", "nominalSignature");
    if (em || ir) {
        QStringList sig;
        if (setAndNonZero(em))
            sig << QStringLiteral("EM: ") + fmt(em);
        if (setAndNonZero(ir))
            sig << QStringLiteral("IR: ") + fmt(ir);
        if (!sig.isEmpty())
            lines << QStringLiteral("Signatures:  ") + sig.join(kPipe);
    }
    return joinLines(lines);
}

QString enhancementsSalvageTool(Node root)
{
    QStringList lines;
    for (const Node fa : findAll(root, ".//SWeaponActionFireSalvageRepairParams")) {
        QString mode = qs(firstSet(fa, {"salvageRepairMode", "name"}).value_or(std::string_view()));
        if (mode.isEmpty())
            mode = QStringLiteral("Mode");
        const auto eff = get(fa, "materialEfficiency");
        const auto hpRate = get(fa, "maxHealthRepairRate");
        const auto dmgRate = get(fa, "maxDamageMapRepairRate");
        const auto h2a = get(fa, "healthToAmmoRatio");
        const auto rampUp = get(fa, "rampUpTime");
        const auto rampDown = get(fa, "rampDownTime");
        QStringList parts;
        if (setAndNonZero(hpRate))
            parts << QStringLiteral("HP Rate: %1/s").arg(fmt(hpRate));
        if (setAndNonZero(dmgRate))
            parts << QStringLiteral("Damage-Map Rate: %1/s").arg(fmt(dmgRate));
        if (eff && *eff != "1" && *eff != "1.0")
            parts << QStringLiteral("Material Efficiency: ") + fmt(eff, {}, 2);
        if (setAndNonZero(h2a))
            parts << QStringLiteral("HP/Ammo: ") + fmt(h2a, {}, 2);
        if (setAndNonZero(rampUp) || setAndNonZero(rampDown))
            parts << QStringLiteral("Ramp: %1 up, %2 down").arg(fmt(rampUp, u"s", 1), fmt(rampDown, u"s", 1));
        energyHeatWear(parts, fa);
        if (!parts.isEmpty())
            lines << mode + QStringLiteral(":  ") + parts.join(kPipe);
    }
    if (const auto hp = attrIn(root, "SHealthComponentParams", "Health"); setAndNonZero(hp))
        lines << QStringLiteral("Component HP: ") + fmt(hp);
    if (const auto wear = attrIn(root, "SWearAccumulatorParams", "MaxLifetimeHours"); setAndNonZero(wear))
        lines << QStringLiteral("Max Lifetime: ") + fmt(wear, u"h", 1);
    return joinLines(lines);
}

QString enhancementsWeapon(Node root, const RecordLookup &ammo, const Loc *loc, const MagazineLookup *magazines)
{
    const std::optional<double> fr = fireRate(root);
    const QStringList modes = fireModes(root, loc);
    const auto pwr = findResource(root, "Power");
    const auto hp = attrIn(root, "SHealthComponentParams", "Health");
    const auto em = attrIn(root, "EMSignature", "nominalSignature");
    const auto ir = attrIn(root, "IRSignature", "nominalSignature");
    const auto overheat = attrIn(root, "itemResourceParams", "overheatTemperature");

    std::optional<double> weight;
    forEachElement(root, [&](Node el) {
        const std::string_view pt = polyType(el);
        if (pt.find("RigidPhysics") == std::string_view::npos && pt.find("StaticPhysics") == std::string_view::npos)
            return true;
        if (const std::string_view mass = getOr(el, "Mass"); !mass.empty())
            weight = toFloat(mass);
        return false;
    });

    qint64 pellets = 1;
    forEachElement(root, [&](Node el) {
        if (polyType(el).find("SProjectileLauncher") == std::string_view::npos)
            return true;
        if (const auto pc = py::toInt(qs(getOr(el, "pelletCount", "1"))); pc && *pc > 1)
            pellets = *pc;
        return false;
    });

    const Node container = findDescendant(root, "SAmmoContainerComponentParams");
    QString ammoId = container ? qs(getOr(container, "ammoParamsRecord")) : QString();
    QString capacity;
    const QString nullUuid = qs(kNullUuid);
    if ((ammoId.isEmpty() || ammoId == nullUuid) && magazines && !magazines->isEmpty()) {
        forEachElement(root, [&](Node el) {
            const QString port = qs(getOr(el, "itemPortName"));
            const QString entityClass = qs(getOr(el, "entityClassName"));
            if (!port.contains(u"magazine", Qt::CaseInsensitive) || entityClass.isEmpty())
                return true;
            if (const auto it = magazines->constFind(entityClass); it != magazines->cend()) {
                ammoId = it->first;
                if (!it->second.isEmpty())
                    capacity = it->second;
            }
            return false;
        });
    }

    std::optional<double> totalDmg;
    DamageBreakdown breakdown;
    std::optional<std::string_view> projSpeed;
    std::optional<std::string_view> projLifetime;
    std::optional<double> dps;
    std::optional<double> dropMinDist, dropPerM, dropMin;
    if (!ammoId.isEmpty() && ammoId != nullUuid) {
        if (const Node ammoRoot = ammo.value(ammoId)) {
            breakdown = ammoDamageBreakdown(ammoRoot);
            totalDmg = breakdown.total;
            if (pellets > 1 && *totalDmg != 0) {
                *totalDmg *= double(pellets);
                for (auto &p : breakdown.parts)
                    p.second *= double(pellets);
            }
            projSpeed = firstSet(ammoRoot, {"speed", "velocity", "projectileSpeed", "initialSpeed"});
            projLifetime = firstSet(ammoRoot, {"lifetime", "projectileLifetime", "maxLifetime"});
            if (*totalDmg != 0 && fr)
                dps = *totalDmg * *fr / 60.0;

            const auto dropOf = [](Node elem) -> std::optional<double> {
                std::optional<double> out;
                for (const Node d : children(elem)) {
                    if (polyType(d) != "DamageInfo" && tag(d).find("DamageInfo") == std::string_view::npos)
                        continue;
                    const auto p = damageValue(d, "DamagePhysical");
                    const auto e = damageValue(d, "DamageEnergy");
                    if (p && e)
                        out = *p + *e;
                }
                return out;
            };
            forEachElement(ammoRoot, [&](Node el) {
                const std::string_view t = tag(el);
                if (t == "damageDropMinDistance") {
                    if (const auto v = dropOf(el))
                        dropMinDist = v;
                } else if (t == "damageDropPerMeter") {
                    if (const auto v = dropOf(el))
                        dropPerM = v;
                } else if (t == "damageDropMinDamage") {
                    if (const auto v = dropOf(el))
                        dropMin = v;
                }
                return true;
            });
        }
    }

    std::optional<std::string_view> regenRate, regenCooldown, regenCost;
    if (const Node regen = findDescendant(root, "SWeaponRegenConsumerParams")) {
        if (capacity.isEmpty())
            capacity = qs(getOr(regen, "maxAmmoLoad"));
        regenRate = get(regen, "requestedRegenPerSec");
        regenCooldown = get(regen, "regenerationCooldown");
        regenCost = get(regen, "regenerationCostPerBullet");
    } else if (container && capacity.isEmpty()) {
        capacity = qs(getOr(container, "maxAmmoCount"));
    }

    QStringList lines;
    if (weight && *weight > 0)
        lines << QStringLiteral("Weight: %1 kg").arg(py::fixed(*weight, 1));
    if (fr)
        lines << QStringLiteral("Fire Rate: ") + fmt(fr, u" RPM");
    if (!modes.isEmpty())
        lines << QStringLiteral("Fire Modes: ") + modes.join(QStringLiteral(" / "));
    if (totalDmg && *totalDmg > 0) {
        const QString pelletStr = pellets > 1 ? QStringLiteral(" x%1").arg(pellets) : QString();
        QStringList parts = {QStringLiteral("Alpha Dmg: ") + fmt(totalDmg, {}, 1) + pelletStr + damageTypeSuffix(breakdown)};
        if (dps && *dps != 0)
            parts << QStringLiteral("DPS: ") + fmt(dps, {}, 1);
        lines << parts.join(kPipe);
    }
    if (!capacity.isEmpty())
        lines << QStringLiteral("Ammo: ") + fmt(std::string_view(capacity.toUtf8().constData()));
    const bool rate = regenRate && !regenRate->empty();
    const bool cool = regenCooldown && !regenCooldown->empty();
    if (rate || cool) {
        QStringList parts;
        if (rate)
            parts << QStringLiteral("Regen: %1/s").arg(fmt(regenRate));
        if (cool)
            parts << QStringLiteral("Cooldown: ") + fmt(regenCooldown, u"s", 1);
        if (regenCost && !regenCost->empty())
            parts << QStringLiteral("Cost/Shot: ") + fmt(regenCost);
        lines << parts.join(kPipe);
    }
    if (projSpeed) {
        const std::optional<double> speed = toFloat(*projSpeed);
        const std::optional<double> life = projLifetime ? toFloat(*projLifetime) : std::nullopt;
        if (speed && life) {
            const double range = *speed * *life;
            const QString label = magazines ? QStringLiteral("Absolute Range") : QStringLiteral("Range");
            const QString rangeText = range >= 1000 ? py::fixed(range / 1000, 1, true) + QStringLiteral(" km")
                                                    : py::fixed(range, 0, true) + QStringLiteral(" m");
            lines << QStringLiteral("Velocity: %1  |  %2: %3").arg(fmt(projSpeed, u" m/s"), label, rangeText);
        }
    }
    if (dropMinDist && *dropMinDist > 0) {
        QStringList parts = {QStringLiteral("Full Dmg to: %1 m").arg(py::fixed(*dropMinDist, 0))};
        if (dropPerM && *dropPerM > 0)
            parts << QStringLiteral("Drop: -%1/m").arg(py::fixed(*dropPerM, 2));
        if (dropMin && *dropMin > 0)
            parts << QStringLiteral("Min Dmg: ") + py::fixed(*dropMin, 1);
        lines << parts.join(kPipe);
    }
    if (pwr && !pwr->empty())
        lines << QStringLiteral("Power Draw: ") + fmt(pwr, u" PU/s");
    if (hp)
        lines << QStringLiteral("Component HP: ") + fmt(hp);
    addSignatures(lines, em, ir);
    if (!magazines)
        addOverheat(lines, overheat);
    return joinLines(lines);
}

std::pair<QString, QString> loadoutSummary(Node root)
{
    using Slot = std::pair<QString, bool>;
    std::vector<Slot> guns, turrets, mracks;
    QStringList shields, powers, coolers, qd;

    const Node comp = findDescendant(root, "SEntityComponentDefaultLoadoutParams");
    if (!comp)
        return {};
    const Node top = find(comp, ".//entries");
    if (!top)
        return {};
    static const QRegularExpression classRe = py::re(QStringLiteral(R"(_class_?(\d+))"));
    for (const Node entry : children(top)) {
        if (tag(entry) != "SItemPortLoadoutEntryParams")
            continue;
        const QString port = qs(getOr(entry, "itemPortName")).toLower();
        const QString cls = qs(getOr(entry, "entityClassName"));
        if (port.contains(u"controller"))
            continue;
        QString size;
        if (const QRegularExpressionMatch m = classRe.match(port); m.hasMatch())
            size = QStringLiteral("S") + QString::number(m.capturedView(1).toLongLong());
        else if (!cls.isEmpty())
            size = extractItemSize(cls);
        const QString sz = size.isEmpty() ? QStringLiteral("?") : size;

        if (cls.startsWith(QStringLiteral("Mount_Gimbal_")) || cls.startsWith(QStringLiteral("Mount_Fixed_")))
            guns.emplace_back(sz, true);
        else if (port.contains(u"weapon_gun"))
            guns.emplace_back(sz, !cls.isEmpty());
        else if (port.contains(u"turret") && !cls.isEmpty())
            turrets.emplace_back(sz, true);
        else if (port.contains(u"missilerack") || port.contains(u"missilelauncher")) {
            if (!cls.isEmpty())
                mracks.emplace_back(sz, true);
        } else if (port.contains(u"shield_generator") && !cls.isEmpty())
            shields << sz;
        else if ((port.contains(u"power_plant") || port.contains(u"powerplant")) && !cls.isEmpty())
            powers << sz;
        else if (port.contains(u"cooler") && !cls.isEmpty())
            coolers << sz;
        else if (port.contains(u"quantum_drive") && !port.contains(u"fuel") && !cls.isEmpty())
            qd << sz;
    }

    const auto summarizeSlots = [](const std::vector<Slot> &slotList) {
        std::map<std::pair<QString, bool>, int, bool (*)(const std::pair<QString, bool> &, const std::pair<QString, bool> &)>
            counts([](const std::pair<QString, bool> &a, const std::pair<QString, bool> &b) {
                if (a.first != b.first)
                    return py::less(a.first, b.first);
                return !a.second && b.second;
            });
        for (const Slot &s : slotList)
            ++counts[s];
        QStringList parts;
        for (const auto &[key, cnt] : counts) {
            const QString suffix = key.second ? QString() : QStringLiteral(" (empty)");
            if (key.first == u"?")
                parts << QString::number(cnt);
            else
                parts << (cnt > 1 ? QStringLiteral("%1× ").arg(cnt) : QString()) + key.first + suffix;
        }
        parts.removeAll(QString());
        return parts.join(QStringLiteral("  "));
    };
    const auto summarizeItems = [](const QStringList &sizes) {
        std::map<QString, int, bool (*)(const QString &, const QString &)> counts(
            [](const QString &a, const QString &b) { return py::less(a, b); });
        for (const QString &s : sizes)
            ++counts[s];
        QStringList parts;
        for (const auto &[sz, cnt] : counts)
            parts << (cnt > 1 ? QStringLiteral("%1× ").arg(cnt) : QString()) + sz;
        return parts.join(QStringLiteral("  "));
    };

    QStringList weapons;
    if (!guns.empty())
        weapons << QStringLiteral("Guns: ") + summarizeSlots(guns);
    if (!turrets.empty())
        weapons << QStringLiteral("Turrets: ") + summarizeSlots(turrets);
    if (!mracks.empty())
        weapons << QStringLiteral("MRacks: ") + summarizeSlots(mracks);
    QStringList core;
    if (!shields.isEmpty())
        core << QStringLiteral("Shields: ") + summarizeItems(shields);
    if (!coolers.isEmpty())
        core << QStringLiteral("Coolers: ") + summarizeItems(coolers);
    if (!powers.isEmpty())
        core << QStringLiteral("Power: ") + summarizeItems(powers);
    if (!qd.isEmpty())
        core << QStringLiteral("QD: ") + summarizeItems(qd);
    return {weapons.join(kPipe), core.join(kPipe)};
}

QString armorStatsBlock(Node armorRoot)
{
    QStringList lines;
    if (const auto health = attrIn(armorRoot, "SHealthComponentParams", "Health"))
        lines << QStringLiteral("Armor HP: ") + fmt(health);
    const auto four = [](Node n) {
        return std::array{get(n, "DamagePhysical"), get(n, "DamageEnergy"), get(n, "DamageDistortion"),
                          get(n, "DamageThermal")};
    };
    if (const Node dm = findDescendant(armorRoot, "damageMultiplier"))
        if (const Node di = find(dm, "DamageInfo")) {
            const auto v = four(di);
            if (v[0] || v[1] || v[2] || v[3])
                lines << QStringLiteral("Dmg Mult: P %1  |  E %2  |  D %3  |  T %4")
                             .arg(fmt(v[0], u"x", 2), fmt(v[1], u"x", 2), fmt(v[2], u"x", 2), fmt(v[3], u"x", 2));
        }
    if (const Node ad = findDescendant(armorRoot, "armorDeflection"))
        if (const Node dv = find(ad, "deflectionValue")) {
            const auto v = four(dv);
            if (v[0] || v[1] || v[2] || v[3])
                lines << QStringLiteral("Deflect: P %1  |  E %2  |  D %3  |  T %4")
                             .arg(fmt(v[0]), fmt(v[1]), fmt(v[2]), fmt(v[3]));
        }
    return joinLines(lines);
}

QString enhancementsShip(Node root, Node controllerRoot, const Loc *loc, const RecordLookup *armor)
{
    const Node vpc = findDescendant(root, "VehicleComponentParams");
    if (!vpc)
        return QString();
    const auto crew = get(vpc, "crewSize");
    const auto stripAt = [](std::string_view s) {
        while (!s.empty() && s.front() == '@')
            s.remove_prefix(1);
        return qs(s);
    };
    const QString careerKey = stripAt(getOr(vpc, "vehicleCareer"));
    const QString roleKey = stripAt(getOr(vpc, "vehicleRole"));
    const QString *career = loc && !careerKey.isEmpty() ? loc->find(careerKey) : nullptr;
    const QString *role = loc && !roleKey.isEmpty() ? loc->find(roleKey) : nullptr;
    const Node bbox = find(vpc, "maxBoundingBoxSize");
    const auto length = bbox ? get(bbox, "y") : std::nullopt;

    const Node ins = findDescendant(root, "shipInsuranceParams");
    const auto insBase = ins ? get(ins, "baseWaitTimeMinutes") : std::nullopt;
    const auto insExpress = ins ? get(ins, "mandatoryWaitTimeMinutes") : std::nullopt;

    const auto [weaponsLine, coreLine] = loadoutSummary(root);

    QString armorBlock;
    if (armor && !armor->byId.isEmpty()) {
        for (const Node entry : iter(root, "SItemPortLoadoutEntryParams")) {
            const std::string_view port = getOr(entry, "itemPortName");
            if (port != "hardpoint_armor" && port != "hardpoint_armour")
                continue;
            const QString armorClass = qs(getOr(entry, "entityClassName")).toLower();
            if (!armorClass.isEmpty())
                if (const Node armorRoot = armor->value(armorClass))
                    armorBlock = armorStatsBlock(armorRoot);
            break;
        }
    }

    std::optional<std::string_view> scm, maxSpd, boostFwd, boostBwd, pitch, roll, yaw;
    if (controllerRoot) {
        if (const Node ifcs = findDescendant(controllerRoot, "IFCSParams")) {
            scm = get(ifcs, "scmSpeed");
            maxSpd = get(ifcs, "maxSpeed");
            boostFwd = get(ifcs, "boostSpeedForward");
            boostBwd = get(ifcs, "boostSpeedBackward");
        }
        if (const Node sp = findByType(controllerRoot, "SIFCSSpeedProfile"))
            if (const Node av = find(sp, "angularVelocity")) {
                pitch = get(av, "x");
                roll = get(av, "y");
                yaw = get(av, "z");
            }
    }

    QStringList lines;
    if (scm || maxSpd)
        lines << QStringLiteral("SCM: %1  |  Max: %2").arg(fmt(scm, u" m/s"), fmt(maxSpd, u" m/s"));
    if (boostFwd || boostBwd)
        lines << QStringLiteral("Boost: +%1  /  -%2").arg(fmt(boostFwd, u" m/s"), fmt(boostBwd, u" m/s"));
    if (pitch)
        lines << QStringLiteral("Pitch: %1  |  Roll: %2  |  Yaw: %3")
                     .arg(fmt(pitch, u"°/s"), fmt(roll, u"°/s"), fmt(yaw, u"°/s"));
    QStringList basics;
    if (crew)
        basics << QStringLiteral("Crew: ") + fmt(crew);
    if (length)
        basics << QStringLiteral("Length: ") + fmt(length, u"m", 1);
    if (career)
        basics << QStringLiteral("Class: ") + *career;
    if (role)
        basics << QStringLiteral("Role: ") + *role;
    if (!basics.isEmpty())
        lines << basics.join(kPipe);
    if (!weaponsLine.isEmpty())
        lines << weaponsLine;
    if (!coreLine.isEmpty())
        lines << coreLine;
    if (!armorBlock.isEmpty())
        lines << armorBlock;
    if (insBase)
        lines << QStringLiteral("Insurance: %1 base  |  %2 express").arg(fmt(insBase, u" min", 2), fmt(insExpress, u" min", 2));
    return joinLines(lines);
}

} // namespace core::enh
