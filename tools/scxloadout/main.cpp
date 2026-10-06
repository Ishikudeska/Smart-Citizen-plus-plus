// scxloadout: the ship loadout catalog outside the app, for checking it
// against real game data. Build the cache with `scxmissions forge`.
//
//   scxloadout ships <cache-dir> <base.ini>             every ship, with a summary
//   scxloadout show <cache-dir> <base.ini> <ship-id>    a ship's stock loadout and totals
//   scxloadout audit <cache-dir> <base.ini>             stock entries the catalog can't place
//   scxloadout compat <cache-dir> <base.ini> <ship-id> <slot-path>   what fits a slot

#include "core/loadout/Catalog.h"
#include "core/loadout/Loadout.h"
#include "core/loadout/Performance.h"
#include "core/pipeline/Extraction.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <cstdio>
#include <map>

using namespace core;
using namespace core::loadout;

namespace {

int usage()
{
    std::fputs("usage:\n"
               "  scxloadout ships <cache-dir> <base.ini>\n"
               "  scxloadout show <cache-dir> <base.ini> <ship-id>\n"
               "  scxloadout audit <cache-dir> <base.ini>\n"
               "  scxloadout compat <cache-dir> <base.ini> <ship-id> <slot-path>\n",
               stderr);
    return 2;
}

QByteArray u8(const QString &s)
{
    return s.toUtf8();
}

Catalog load(const QString &cache, const IniMap &loc)
{
    QElapsedTimer timer;
    timer.start();
    Catalog catalog = buildCatalog({dataForgeRecordsDir(cache), dataForgeVehiclesDir(cache), &loc});
    std::fprintf(stderr, "built in %.2fs: %zu ships, %zu items, vehicles: %s\n", timer.elapsed() / 1000.0,
                 catalog.ships.size(), catalog.items.size(), catalog.hasVehicles ? "yes" : "no");
    return catalog;
}

int ships(const QString &cache, const QString &baseIni)
{
    const IniMap loc = loadIni(baseIni);
    const Catalog catalog = load(cache, loc);
    int noPorts = 0, noLoadout = 0;
    for (const Ship &s : catalog.ships) {
        const Loadout l(catalog, s);
        const Totals t = computeTotals(l, consumers(l, {}));
        int editable = 0;
        l.forEach([&](const Slot &slot, int) { editable += slot.port.editable && !slot.port.hidden; });
        noPorts += s.ports.empty();
        noLoadout += s.loadout.empty();
        std::printf("%-40s %-34s S%d crew %d  ports %3zu  dps %6.0f/%6.0f  shield %6.0f  hull %6.0f  "
                    "power %2d/%2.0f  scm %4.0f  qt %5.1f Gm%s\n",
                    u8(s.id).constData(), u8(s.name).constData(), s.size, s.crew, s.ports.size(), t.burstDps,
                    t.sustainedDps, t.shieldHp, t.hullHp, t.powerUsed, t.powerOutput,
                    t.flight ? t.flight->scmSpeed : 0, t.quantumRange, s.groundVehicle ? "  (ground)" : "");
    }
    std::fprintf(stderr, "%d ships without ports, %d without a stock loadout\n", noPorts, noLoadout);
    return 0;
}

QString damageText(const Damage &d)
{
    QStringList parts;
    const std::pair<const char *, double> types[] = {{"phys", d.physical},   {"energy", d.energy},
                                                     {"dist", d.distortion}, {"therm", d.thermal},
                                                     {"bio", d.biochemical}, {"stun", d.stun}};
    for (const auto &[name, v] : types)
        if (v != 0)
            parts << QStringLiteral("%1 %2").arg(QLatin1StringView(name)).arg(v, 0, 'g', 4);
    return parts.join(QStringLiteral(", "));
}

QString itemLine(const Item &i)
{
    QString s = QStringLiteral("%1 [%2 S%3%4 %5]")
                    .arg(i.name, i.id, QString::number(i.size),
                         i.gradeLetter().isEmpty() ? QString() : u' ' + i.gradeLetter(),
                         i.type + (i.subType.isEmpty() ? QString() : u':' + i.subType));
    if (!i.manufacturerCode.isEmpty())
        s += u' ' + i.manufacturerCode;
    if (!i.itemClass.isEmpty())
        s += QStringLiteral(" · ") + i.itemClass;
    const Resources &r = i.resources;
    if (r.powerDraw > 0)
        s += QStringLiteral("  pwr %1").arg(r.powerDraw);
    if (r.powerOutput > 0)
        s += QStringLiteral("  gen %1").arg(r.powerOutput);
    if (r.coolantOutput > 0)
        s += QStringLiteral("  cool %1").arg(r.coolantOutput);
    if (r.em > 0 || r.ir > 0)
        s += QStringLiteral("  em %1 ir %2").arg(r.em).arg(r.ir);
    if (const auto &w = i.weapon)
        s += QStringLiteral("  alpha %1 (%2) rate %3 burst %4 sus %5 ammo %6 v %7 range %8 pen %9 ovh %10s")
                 .arg(w->alpha(), 0, 'f', 1)
                 .arg(damageText(w->damage))
                 .arg(w->fireRate)
                 .arg(w->burstDps(), 0, 'f', 0)
                 .arg(w->sustainedDps(), 0, 'f', 0)
                 .arg(w->ammo)
                 .arg(w->speed)
                 .arg(w->range, 0, 'f', 0)
                 .arg(w->penetration)
                 .arg(w->timeToOverheat(), 0, 'f', 1);
    if (const auto &m = i.missile)
        s += QStringLiteral("  dmg %1 %2 lock %3s %4° %5-%6 m v %7")
                 .arg(m->damage.total())
                 .arg(m->tracking)
                 .arg(m->lockTime)
                 .arg(m->lockAngle)
                 .arg(m->lockRangeMin)
                 .arg(m->lockRangeMax)
                 .arg(m->speed);
    if (const auto &sh = i.shield)
        s += QStringLiteral("  hp %1 regen %2 delay %3/%4 res(%5) abs(%6)")
                 .arg(sh->hp)
                 .arg(sh->regen)
                 .arg(sh->damagedDelay)
                 .arg(sh->downedDelay)
                 .arg(damageText(sh->resistanceMax), damageText(sh->absorptionMax));
    if (const auto &q = i.quantum)
        s += QStringLiteral("  speed %1 km/s spool %2 cd %3 a1 %4 a2 %5 spline %6 fuel %7 SCU/Gm")
                 .arg(q->speed / 1000, 0, 'f', 0)
                 .arg(q->spoolTime)
                 .arg(q->cooldown)
                 .arg(q->stageOneAccel / 1000, 0, 'f', 0)
                 .arg(q->stageTwoAccel / 1000, 0, 'f', 0)
                 .arg(q->splineSpeed / 1000, 0, 'f', 0)
                 .arg(q->fuelPerGm);
    if (const auto &t = i.thruster)
        s += QStringLiteral("  %1 %2 MN").arg(t->kind).arg(t->thrust / 1e6, 0, 'f', 3);
    if (const auto &a = i.armor)
        s += QStringLiteral("  hp %1 mult(%2) defl(%3) sig %4/%5/%6")
                 .arg(i.health)
                 .arg(damageText(a->multiplier), damageText(a->deflection))
                 .arg(a->emMultiplier)
                 .arg(a->irMultiplier)
                 .arg(a->csMultiplier);
    if (const auto &f = i.flight)
        s += QStringLiteral("  scm %1 boost %2/%3 max %4 p/y/r %5/%6/%7")
                 .arg(f->scmSpeed)
                 .arg(f->boostForward)
                 .arg(f->boostBackward)
                 .arg(f->maxSpeed)
                 .arg(f->pitch)
                 .arg(f->yaw)
                 .arg(f->roll);
    if (const auto &c = i.countermeasure)
        s += QStringLiteral("  %1 x%2").arg(c->kind).arg(c->ammo);
    if (i.fuelCapacity > 0)
        s += QStringLiteral("  %1 SCU").arg(i.fuelCapacity);
    return s;
}

int show(const QString &cache, const QString &baseIni, const QString &id)
{
    const IniMap loc = loadIni(baseIni);
    const Catalog catalog = load(cache, loc);
    const Ship *ship = catalog.ship(id);
    if (!ship) {
        std::fprintf(stderr, "no ship %s\n", u8(id).constData());
        return 1;
    }
    std::printf("%s (%s) — %s, %s %s, S%d, crew %d, %.1f x %.1f x %.1f m, %.0f kg, hull %.0f (vital %.0f)\n",
                u8(ship->name).constData(), u8(ship->id).constData(), u8(ship->manufacturer).constData(),
                u8(ship->career).constData(), u8(ship->role).constData(), ship->size, ship->crew,
                ship->length, ship->width, ship->height, ship->mass, ship->hullHp(), ship->vitalHp());
    for (const PowerPool &p : ship->powerPools)
        std::printf("  pool %s %s %d\n", u8(p.itemType).constData(), p.fixed ? "fixed" : "dynamic", p.size);
    const Loadout l(catalog, *ship);
    l.forEach([&](const Slot &s, int depth) {
        if (!s.item && !s.port.editable)
            return;
        std::printf("%*s%s%s [S%d-%d%s]: %s\n", depth * 2 + 2, "", u8(s.port.name).constData(),
                    s.port.editable ? "" : " (fixed)", s.port.minSize, s.port.maxSize,
                    s.port.hidden ? " hidden" : "", s.item ? u8(itemLine(*s.item)).constData() : "-");
    });
    const std::vector<Consumer> power = consumers(l, {});
    for (const Consumer &c : power)
        std::printf("  power %-10s %-30s %d/%d (min %d)\n", u8(c.category).constData(),
                    u8(c.label).constData(), c.pips, c.max, c.min);
    const Totals t = computeTotals(l, power);
    std::printf(
        "damage: alpha %.0f burst %.0f sustained %.0f (pilot %d guns %.0f, manned %d, remote %d, pds %d) "
        "missiles %d (%.0f) bombs %d\n",
        t.alpha, t.burstDps, t.sustainedDps, t.pilot.guns, t.pilot.burst, t.manned.guns, t.remote.guns,
        t.pds.guns, t.missiles, t.missileDamage, t.bombs);
    std::printf("defense: shield %.0f regen %.0f (%d gens) res(%s) abs(%s) armor %.0f mult(%s) defl(%s) "
                "decoys %d noise %d\n",
                t.shieldHp, t.shieldRegen, t.shieldGenerators, u8(damageText(t.shieldResistance)).constData(),
                u8(damageText(t.shieldAbsorption)).constData(), t.armorHp,
                u8(damageText(t.armorMultiplier)).constData(), u8(damageText(t.armorDeflection)).constData(),
                t.decoys, t.noise);
    std::printf("power %d/%.0f, coolant %.1f, em %.0f, ir %.0f\n", t.powerUsed, t.powerOutput,
                t.coolantOutput, t.em, t.ir);
    std::printf("flight: mass %.0f/%.0f thrust main %.2f retro %.3f mav %.2f MN accel %.1f m/s² "
                "fuel %.2f qt %.2f SCU range %.0f Gm\n",
                t.hullMass, t.loadedMass, t.mainThrust / 1e6, t.retroThrust / 1e6, t.maneuverThrust / 1e6,
                t.accelForward, t.hydrogenFuel, t.quantumFuel, t.quantumRange);
    if (t.quantum)
        std::printf("quantum: 20 Gm in %.0f s, 1 Gm in %.0f s\n", quantumTravelTime(*t.quantum, 20e9),
                    quantumTravelTime(*t.quantum, 1e9));
    const Engagement self = engage(t, t);
    std::printf("vs itself: shields down %.1f s, kill %.1f s, deflected guns %d\n", self.shieldTime,
                self.killTime, self.deflected);
    return 0;
}

// What fits one of a ship's slots.
int compat(const QString &cache, const QString &baseIni, const QString &id, const QString &path)
{
    const IniMap loc = loadIni(baseIni);
    const Catalog catalog = load(cache, loc);
    const Ship *ship = catalog.ship(id);
    if (!ship)
        return 1;
    const Loadout l(catalog, *ship);
    for (const Item *i : l.compatible(path))
        std::printf("  %s\n", u8(itemLine(*i)).left(160).constData());
    return 0;
}

// Stock loadout entries the catalog can't place: items it doesn't know,
// ports the ship or the item doesn't have.
int audit(const QString &cache, const QString &baseIni)
{
    const IniMap loc = loadIni(baseIni);
    const Catalog catalog = load(cache, loc);
    std::map<QString, int> unknownItems, lostPorts;
    int shipsWithProblems = 0;
    for (const Ship &s : catalog.ships) {
        QStringList problems;
        const auto check = [&](const auto &self, const std::vector<LoadoutEntry> &entries, const Item *parent,
                               const QString &path) -> void {
            for (const LoadoutEntry &e : entries) {
                const QString here = path.isEmpty() ? e.port : path + u'/' + e.port;
                const Port *port = nullptr;
                for (const Port &p : parent ? parent->ports : s.ports)
                    if (p.name.compare(e.port, Qt::CaseInsensitive) == 0)
                        port = &p;
                const bool known = port != nullptr;
                const Item *item = stockItem(catalog, s.portTags, port, e);
                if (!e.item.isEmpty() && !e.refItem.isEmpty() &&
                    e.item.compare(e.refItem, Qt::CaseInsensitive) != 0 && catalog.item(e.item) &&
                    catalog.item(e.refItem))
                    std::printf("PICK %s %s: name %s, ref %s -> %s\n", u8(s.id).constData(),
                                u8(here).constData(), u8(e.item).constData(), u8(e.refItem).constData(),
                                item ? u8(item->id).constData() : "-");
                if (!item && !(e.item.isEmpty() && e.refItem.isEmpty())) {
                    ++unknownItems[e.item.isEmpty() ? e.refItem : e.item];
                    continue; // its children can't be placed either way
                }
                if (item && port && !fits(*port, *item, s.portTags)) {
                    QStringList types;
                    for (const PortType &t : port->types)
                        types << t.type + u':' + t.subTypes.join(u',');
                    problems << QStringLiteral(
                                    "%1 = %2 (does not fit the port) port[S%3-%4 %5 req=%6 offers=%7] "
                                    "item[S%8 %9:%10 tags=%11 req=%12] ship[%13]")
                                    .arg(here, item->id)
                                    .arg(port->minSize)
                                    .arg(port->maxSize)
                                    .arg(types.join(u' '), port->requiredTags.join(u','),
                                         port->portTags.join(u','))
                                    .arg(item->size)
                                    .arg(item->type, item->subType, item->tags.join(u','),
                                         item->requiredTags.join(u','), s.portTags.join(u','));
                }
                if (!known && item && (parent || item->named)) {
                    ++lostPorts[(parent ? parent->id + QStringLiteral(" > ") : QString()) + e.port];
                    problems << QStringLiteral("%1 = %2 (no such port)").arg(here, e.item);
                }
                if (item)
                    self(self, e.children, item, here);
            }
        };
        check(check, s.loadout, nullptr, QString());
        if (!problems.isEmpty()) {
            ++shipsWithProblems;
            std::printf("%s:\n  %s\n", u8(s.id).constData(),
                        u8(problems.join(QStringLiteral("\n  "))).constData());
        }
    }
    std::printf("\n%d ships with entries on ports they don't have\n", shipsWithProblems);
    std::printf("unknown items (%zu):\n", unknownItems.size());
    for (const auto &[id, n] : unknownItems)
        std::printf("  %4d %s\n", n, u8(id).constData());
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() >= 4 && args[1] == u"ships")
        return ships(args[2], args[3]);
    if (args.size() >= 6 && args[1] == u"compat")
        return compat(args[2], args[3], args[4], args[5]);
    if (args.size() >= 4 && args[1] == u"audit")
        return audit(args[2], args[3]);
    if (args.size() >= 5 && args[1] == u"show")
        return show(args[2], args[3], args[4]);
    return usage();
}
