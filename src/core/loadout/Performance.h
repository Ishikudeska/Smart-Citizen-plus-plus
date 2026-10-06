#pragma once

#include "core/loadout/Loadout.h"

#include <QHash>
#include <QString>

#include <optional>
#include <vector>

// What a loadout adds up to: damage, defense, power and cooling, flight and
// quantum travel, from the fitted items' records. Per-item figures are the
// game's; the totals that depend on how CIG combines them (power effects,
// signatures, travel time, time to kill) are this app's model of it, as noted
// on each.
namespace core::loadout {

// A column on the power screen: the weapons pool, or one powered item.
struct Consumer
{
    QString key;      // "weapons", or the item's slot path
    QString label;    // the item's name; "" for the weapons pool
    QString category; // "weapons", "flight", "shield", "quantum", "radar", "lifesupport", "cooler", "utility"
    int min = 0;      // pips it needs to run at all (0 or from this to max)
    int max = 0;      // pips at full power
    int pips = 0;     // pips given
};

struct PowerPlan
{
    QHash<QString, int> pips; // Consumer::key -> pips; the rest get the default
    bool nav = false;         // NAV mode: weapons and shields off, the quantum drive on
};

// The loadout's power consumers, pips from `plan`. The rest get the game's
// default split while the power plants have pips: coolers 1 pip each,
// shields and life support full, radar its minimum, the weapon pool and
// engines half (the quantum drive full in NAV mode); then weapons, engines,
// radar, coolers and utility fill up in that order.
std::vector<Consumer> consumers(const Loadout &loadout, const PowerPlan &plan);

// A component's share of its output at `pips` (its power ranges).
double powerModifier(const Resources &resources, int pips);

struct WeaponFit
{
    QString path, name;
    QString group; // "pilot", "manned" (crewed turret), "remote", "pds"
    int size = 0;
    Damage perShot; // all pellets
    int pellets = 1;
    double alpha = 0, burst = 0, sustained = 0;
};

struct Totals
{
    // Damage: every fitted gun, by who fires it.
    std::vector<WeaponFit> weapons;
    struct Group
    {
        int guns = 0;
        double alpha = 0, burst = 0, sustained = 0;
    };
    Group pilot, manned, remote, pds;
    double alpha = 0, burstDps = 0, sustainedDps = 0;
    Damage burstByType;
    double weaponEfficiency = 1; // weapon pool pips / pool size: scales capacitor refill
    int missiles = 0, bombs = 0;
    double missileDamage = 0, bombDamage = 0;

    // Defense.
    double shieldHp = 0, shieldRegen = 0; // powered generators only
    int shieldGenerators = 0;
    Damage shieldResistance, shieldAbsorption; // the best fitted generator's maximums
    double armorHp = 0;
    Damage armorMultiplier{1, 1, 1, 1, 1, 1}, armorDeflection;
    double emMultiplier = 1, irMultiplier = 1, csMultiplier = 1;
    int decoys = 0, noise = 0;
    double hullHp = 0, vitalHp = 0;

    // Power, cooling, signatures (model).
    double powerOutput = 0;
    int powerUsed = 0;
    double coolantOutput = 0;
    double em = 0, ir = 0;

    // Flight.
    std::optional<FlightStats> flight;
    double mainThrust = 0, retroThrust = 0, maneuverThrust = 0, vtolThrust = 0; // N
    double hullMass = 0, loadedMass = 0;                                        // kg
    double accelForward = 0;                                                    // m/s², main thrusters
    double hydrogenFuel = 0, quantumFuel = 0;                                   // SCU

    // Quantum travel.
    std::optional<QuantumStats> quantum;
    double quantumRange = 0; // Gm on a full tank
};

Totals computeTotals(const Loadout &loadout, const std::vector<Consumer> &consumers);

// Seconds of quantum travel over `metres`, spool not included (model: stage
// one acceleration to half the drive speed, stage two to full, braking the
// same way).
double quantumTravelTime(const QuantumStats &drive, double metres);

// Your sustained fire against a target ship (model): shields take the
// absorbed share less their resistance, the rest bleeds to the hull; the hull
// takes damage through its armor, and a hit doing less than the armor's
// deflection for its type glances off. Shield regeneration is left out, as
// under continuous fire.
struct Engagement
{
    double shieldTime = 0; // s until the shields drop; 0 when they don't
    double killTime = 0;   // s until the vital hull is gone; 0 when never
    int deflected = 0;     // guns whose every hit glances off the armor
};
Engagement engage(const Totals &attacker, const Totals &target);

} // namespace core::loadout
