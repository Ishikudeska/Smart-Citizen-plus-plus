#include "core/loadout/Performance.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace core::loadout {

namespace {

QString categoryOf(const Item &item)
{
    const QString &t = item.type;
    if (t == u"Shield")
        return QStringLiteral("shield");
    if (t == u"Cooler")
        return QStringLiteral("cooler");
    if (t == u"Radar")
        return QStringLiteral("radar");
    if (t == u"LifeSupportGenerator")
        return QStringLiteral("lifesupport");
    if (t == u"QuantumDrive")
        return QStringLiteral("quantum");
    if (t == u"FlightController")
        return QStringLiteral("flight");
    if (t == u"WeaponMining" || t == u"TractorBeam" || t == u"TowingBeam" || t == u"SalvageHead" ||
        t == u"ToolArm" || t == u"EMP" || t == u"QuantumInterdictionGenerator" || t == u"UtilityTurret")
        return QStringLiteral("utility");
    return QString();
}

// Who fires a gun: the turret it sits in, if any.
QString groupOf(const std::vector<const Item *> &ancestors)
{
    for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
        const Item *a = *it;
        if (a->type != u"Turret" && a->type != u"TurretBase")
            continue;
        const QString sub = a->subType.toLower();
        if (sub.contains(u"pdc") || sub.contains(u"pds"))
            return QStringLiteral("pds");
        if (sub.contains(u"remote"))
            return QStringLiteral("remote");
        if (sub.contains(u"manned"))
            return QStringLiteral("manned");
    }
    return QStringLiteral("pilot");
}

template <class Visit>
void walk(const std::vector<Slot> &list, std::vector<const Item *> &ancestors, Visit &visit)
{
    for (const Slot &s : list) {
        visit(s, ancestors);
        if (s.item) {
            ancestors.push_back(s.item);
            walk(s.children, ancestors, visit);
            ancestors.pop_back();
        }
    }
}

const PowerPool *weaponPool(const Ship &ship)
{
    for (const PowerPool &p : ship.powerPools)
        if (p.fixed && p.itemType == u"WeaponGun")
            return &p;
    return nullptr;
}

Damage maxOf(const Damage &a, const Damage &b)
{
    return {std::max(a.physical, b.physical),       std::max(a.energy, b.energy),
            std::max(a.distortion, b.distortion),   std::max(a.thermal, b.thermal),
            std::max(a.biochemical, b.biochemical), std::max(a.stun, b.stun)};
}

} // namespace

double powerModifier(const Resources &resources, int pips)
{
    if (pips <= 0)
        return 0;
    double modifier = resources.ranges.empty() ? 1 : resources.ranges.front().modifier;
    for (const PowerRange &r : resources.ranges)
        if (pips >= r.start)
            modifier = r.modifier;
    return modifier;
}

std::vector<Consumer> consumers(const Loadout &loadout, const PowerPlan &plan)
{
    std::vector<Consumer> out;
    if (!loadout.ship())
        return out;
    double weaponDraw = 0;
    loadout.forEach([&](const Slot &s, int) {
        if (!s.item)
            return;
        if (s.item->type == u"WeaponGun") {
            weaponDraw += s.item->resources.powerDraw;
            return;
        }
        const QString category = categoryOf(*s.item);
        const int max = int(std::ceil(s.item->resources.powerDraw - 1e-9));
        if (category.isEmpty() || max <= 0)
            return;
        const int min = std::max(1, int(std::ceil(s.item->resources.powerMinFraction * max - 1e-9)));
        out.push_back({s.path, s.item->name, category, std::min(min, max), max, 0});
    });
    const PowerPool *pool = weaponPool(*loadout.ship());
    const int weaponMax = pool ? pool->size : int(std::ceil(weaponDraw - 1e-9));
    if (weaponMax > 0)
        out.insert(out.begin(), Consumer{QStringLiteral("weapons"), QString(), QStringLiteral("weapons"), 1,
                                         weaponMax, 0});

    double output = 0;
    loadout.forEach([&](const Slot &s, int) {
        if (s.item)
            output += s.item->resources.powerOutput;
    });
    int budget = int(std::floor(output + 1e-9));
    // Up to `want` pips, never below the consumer's minimum once on.
    const auto give = [&](Consumer &c, int want) {
        want = std::min(want, c.max);
        if (want <= c.pips)
            return;
        const int add = std::max(want, c.min) - c.pips;
        if (add > budget)
            return;
        c.pips += add;
        budget -= add;
    };
    // Explicit pips first, then the defaults in order.
    std::vector<Consumer *> open;
    for (Consumer &c : out) {
        if (const auto it = plan.pips.constFind(c.key); it != plan.pips.cend()) {
            c.pips = *it <= 0 ? 0 : std::clamp(*it, c.min, c.max);
            budget -= c.pips;
        } else {
            open.push_back(&c);
        }
    }
    const auto offInMode = [&](const Consumer &c) {
        return plan.nav ? (c.category == u"weapons" || c.category == u"shield") : c.category == u"quantum";
    };
    const auto each = [&](const char *category, auto &&how) {
        for (Consumer *c : open)
            if (c->category == QLatin1StringView(category) && !offInMode(*c))
                how(*c);
    };
    each("cooler", [&](Consumer &c) { give(c, 1); });
    each("shield", [&](Consumer &c) { give(c, c.max); });
    each("lifesupport", [&](Consumer &c) { give(c, c.max); });
    each("radar", [&](Consumer &c) { give(c, c.min); });
    each("weapons", [&](Consumer &c) { give(c, (c.max + 1) / 2); });
    each("flight", [&](Consumer &c) { give(c, (c.max + 1) / 2); });
    each("quantum", [&](Consumer &c) { give(c, c.max); });
    for (const char *category : {"weapons", "flight", "radar", "cooler", "utility"})
        each(category, [&](Consumer &c) {
            while (c.pips < c.max && budget > 0)
                give(c, c.pips + 1);
        });
    return out;
}

Totals computeTotals(const Loadout &loadout, const std::vector<Consumer> &power)
{
    Totals t;
    const Ship *ship = loadout.ship();
    if (!ship)
        return t;
    QHash<QString, const Consumer *> byKey;
    for (const Consumer &c : power) {
        byKey.insert(c.key, &c);
        t.powerUsed += c.pips;
    }
    const auto pipsFor = [&](const QString &key, int fallback) {
        const auto it = byKey.constFind(key);
        return it == byKey.cend() ? fallback : (*it)->pips;
    };
    if (const auto it = byKey.constFind(QStringLiteral("weapons")); it != byKey.cend())
        t.weaponEfficiency = (*it)->max > 0 ? double((*it)->pips) / (*it)->max : 1;

    t.hullHp = ship->hullHp();
    t.vitalHp = ship->vitalHp();
    t.hullMass = ship->mass;
    t.loadedMass = ship->mass;

    double weaponEm = 0, weaponIr = 0, plantEm = 0, plantIr = 0;
    std::vector<const Item *> ancestors;
    const auto visit = [&](const Slot &s, const std::vector<const Item *> &up) {
        const Item *item = s.item;
        if (!item)
            return;
        t.loadedMass += item->mass;
        const Resources &r = item->resources;
        if (item->weapon) {
            const WeaponStats &w = *item->weapon;
            WeaponFit f;
            f.path = s.path;
            f.name = item->name;
            f.group = groupOf(up);
            f.size = item->size;
            f.perShot = w.damage;
            f.pellets = w.pellets;
            f.alpha = w.alpha();
            f.burst = w.burstDps();
            f.sustained = w.sustainedDps(t.weaponEfficiency);
            Totals::Group &g = f.group == u"manned"   ? t.manned
                               : f.group == u"remote" ? t.remote
                               : f.group == u"pds"    ? t.pds
                                                      : t.pilot;
            ++g.guns;
            g.alpha += f.alpha;
            g.burst += f.burst;
            g.sustained += f.sustained;
            t.alpha += f.alpha;
            t.burstDps += f.burst;
            t.sustainedDps += f.sustained;
            if (w.fireRate > 0)
                t.burstByType += w.damage * (w.fireRate / 60.0);
            t.weapons.push_back(std::move(f));
            weaponEm += r.em;
            weaponIr += r.ir;
        }
        if (item->missile) {
            if (item->type == u"Bomb") {
                ++t.bombs;
                t.bombDamage += item->missile->damage.total();
            } else {
                ++t.missiles;
                t.missileDamage += item->missile->damage.total();
            }
        }
        if (item->countermeasure) {
            (item->countermeasure->kind == u"Noise" ? t.noise : t.decoys) += item->countermeasure->ammo;
        }
        if (item->armor) {
            t.armorHp += item->health;
            t.armorMultiplier = item->armor->multiplier;
            t.armorDeflection = item->armor->deflection;
            t.emMultiplier = item->armor->emMultiplier;
            t.irMultiplier = item->armor->irMultiplier;
            t.csMultiplier = item->armor->csMultiplier;
        }
        if (item->thruster) {
            const QString kind = item->thruster->kind.toLower();
            double &sum = kind == u"main"    ? t.mainThrust
                          : kind == u"retro" ? t.retroThrust
                          : kind == u"vtol"  ? t.vtolThrust
                                             : t.maneuverThrust;
            sum += item->thruster->thrust;
        }
        if (item->flight && !t.flight)
            t.flight = item->flight;
        if (item->quantum && !t.quantum)
            t.quantum = item->quantum;
        if (item->type == u"FuelTank")
            t.hydrogenFuel += item->fuelCapacity;
        else if (item->type == u"QuantumFuelTank")
            t.quantumFuel += item->fuelCapacity;
        if (r.powerOutput > 0) {
            t.powerOutput += r.powerOutput;
            plantEm += r.em;
            plantIr += r.ir;
        }

        // Powered components: their share of output and signature.
        const QString category = categoryOf(*item);
        if (category.isEmpty() || item->type == u"WeaponGun")
            return;
        const int max = int(std::ceil(r.powerDraw - 1e-9));
        const int pips = max > 0 ? pipsFor(s.path, max) : 1;
        const double share = max > 0 ? double(pips) / max : 1;
        if (pips > 0) {
            t.em += r.em * share;
            t.ir += r.ir * share;
        }
        if (item->shield && pips > 0) {
            ++t.shieldGenerators;
            t.shieldHp += item->shield->hp;
            t.shieldRegen += item->shield->regen * powerModifier(r, pips);
            t.shieldResistance = maxOf(t.shieldResistance, item->shield->resistanceMax);
            t.shieldAbsorption = maxOf(t.shieldAbsorption, item->shield->absorptionMax);
        }
        if (item->type == u"Cooler")
            t.coolantOutput += r.coolantOutput * powerModifier(r, pips);
    };
    walk(loadout.roots(), ancestors, visit);

    // Weapons and power plants run in proportion to the power they get.
    t.em += weaponEm * t.weaponEfficiency;
    t.ir += weaponIr * t.weaponEfficiency;
    if (t.powerOutput > 0) {
        const double load = std::min(1.0, t.powerUsed / t.powerOutput);
        t.em += plantEm * load;
        t.ir += plantIr * load;
    }
    t.em *= t.emMultiplier;
    t.ir *= t.irMultiplier;

    if (t.loadedMass > 0)
        t.accelForward = t.mainThrust / t.loadedMass;
    if (t.quantum && t.quantum->fuelPerGm > 0)
        t.quantumRange = t.quantumFuel / t.quantum->fuelPerGm;
    return t;
}

double quantumTravelTime(const QuantumStats &drive, double metres)
{
    const double v = drive.speed;
    const double a1 = drive.stageOneAccel > 0 ? drive.stageOneAccel : drive.stageTwoAccel;
    const double a2 = drive.stageTwoAccel > 0 ? drive.stageTwoAccel : a1;
    if (v <= 0 || a1 <= 0 || metres <= 0)
        return 0;
    const double v1 = v / 2;
    const double d1 = v1 * v1 / (2 * a1), t1 = v1 / a1;
    const double d2 = (v * v - v1 * v1) / (2 * a2), t2 = (v - v1) / a2;
    const double ramp = 2 * (d1 + d2);
    if (metres >= ramp)
        return 2 * (t1 + t2) + (metres - ramp) / v;
    const double half = metres / 2;
    if (half >= d1) {
        const double peak = std::sqrt(v1 * v1 + 2 * a2 * (half - d1));
        return 2 * (t1 + (peak - v1) / a2);
    }
    return 2 * std::sqrt(2 * half / a1);
}

Engagement engage(const Totals &attacker, const Totals &target)
{
    Engagement e;
    double shieldRate = 0, bleedRate = 0, hullRate = 0;
    const auto per = [](const Damage &d, int i) {
        const double v[] = {d.physical, d.energy, d.distortion, d.thermal, d.biochemical, d.stun};
        return v[i];
    };
    for (const WeaponFit &w : attacker.weapons) {
        if (w.alpha <= 0 || w.sustained <= 0)
            continue;
        const double shotsPerSecond = w.sustained / w.alpha;
        bool glances = true;
        for (int i = 0; i < 6; ++i) {
            const double shot = per(w.perShot, i);
            if (shot <= 0)
                continue;
            const double dps = shot * shotsPerSecond;
            const double absorb = std::clamp(per(target.shieldAbsorption, i), 0.0, 1.0);
            const double resist = std::clamp(per(target.shieldResistance, i), 0.0, 1.0);
            shieldRate += dps * absorb * (1 - resist);
            const bool deflects = shot / std::max(1, w.pellets) < per(target.armorDeflection, i);
            if (deflects)
                continue;
            glances = false;
            const double toHull = dps * per(target.armorMultiplier, i);
            bleedRate += toHull * (1 - absorb);
            hullRate += toHull;
        }
        if (glances)
            ++e.deflected;
    }
    const double hull = target.vitalHp > 0 ? target.vitalHp : target.hullHp;
    if (target.shieldHp > 0) {
        if (shieldRate <= 0) {
            e.killTime = bleedRate > 0 ? hull / bleedRate : 0;
            return e;
        }
        e.shieldTime = target.shieldHp / shieldRate;
    }
    const double left = hull - bleedRate * e.shieldTime;
    if (left <= 0)
        e.killTime = bleedRate > 0 ? hull / bleedRate : e.shieldTime;
    else
        e.killTime = hullRate > 0 ? e.shieldTime + left / hullRate : 0;
    return e;
}

} // namespace core::loadout
