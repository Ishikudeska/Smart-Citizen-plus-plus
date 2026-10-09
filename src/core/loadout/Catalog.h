#pragma once

#include "core/text/IniFile.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <atomic>
#include <optional>
#include <vector>

// The ship loadout catalog: every player ship with its hardpoints (from the
// vehicle implementation XMLs, variants applied) and stock loadout, and every
// ship item with the stats a loadout is judged by, read from the DataForge
// cache. Text is in the language of the base.ini it is given.
namespace core::loadout {

// Per damage type: damage, resistances, multipliers.
struct Damage
{
    double physical = 0, energy = 0, distortion = 0, thermal = 0, biochemical = 0, stun = 0;

    double total() const { return physical + energy + distortion + thermal + biochemical + stun; }
    Damage operator*(double f) const
    {
        return {physical * f, energy * f, distortion * f, thermal * f, biochemical * f, stun * f};
    }
    Damage &operator+=(const Damage &o)
    {
        physical += o.physical;
        energy += o.energy;
        distortion += o.distortion;
        thermal += o.thermal;
        biochemical += o.biochemical;
        stun += o.stun;
        return *this;
    }
    bool operator==(const Damage &) const = default;
};

// An item type a port takes: "WeaponGun" with subtypes {"Gun"}; no subtypes
// for any.
struct PortType
{
    QString type;
    QStringList subTypes;
};

struct Port
{
    QString name;  // "hardpoint_weapon_class2_nose"
    QString label; // the port's display name; may be empty
    int minSize = 0, maxSize = 0;
    std::vector<PortType> types; // empty: anything
    QStringList portTags;        // tags the port offers an item's RequiredTags
    QStringList requiredTags;    // tags an item must carry
    bool editable = true;        // players can swap what's in it
    bool hidden = false;         // "invisible": not shown on the ship's HUD
};

// One step of a component's output by the power it gets: from `start` pips
// up it works at `modifier` of its full output.
struct PowerRange
{
    double start = 0, modifier = 1;
};

// The item's place in the 4.x resource network: its first (online) state.
struct Resources
{
    double powerDraw = 0;        // power segments it takes at full power
    double powerMinFraction = 0; // of powerDraw, while on
    double powerOutput = 0;      // power segments it generates
    double coolantOutput = 0;    // coolant it generates
    double quantumFuel = 0;      // its quantum fuel use, in the record's micro units
    double em = 0, ir = 0;       // nominal signatures while on
    std::vector<PowerRange> ranges;
};

struct WeaponStats
{
    QString fireMode;    // the default fire action's name ("Rapid")
    double fireRate = 0; // shots a minute
    int pellets = 1;
    Damage damage;    // a shot, all pellets
    double speed = 0; // m/s
    double range = 0; // m: speed × lifetime
    double penetration = 0;
    int ammo = 0;              // magazine, or capacitor shots for a regenerating weapon
    bool regenerates = false;  // refills its capacitor from the weapon power pool
    double regenPerSecond = 0; // capacitor shots refilled a second at full power
    double regenCooldown = 0;  // s after firing before it refills
    double heatPerShot = 0;    // toward overheatTemperature
    double overheatTemperature = 0;
    double overheatFixTime = 0; // s locked out once overheated

    double alpha() const { return damage.total(); }
    double burstDps() const { return alpha() * fireRate / 60.0; }
    // s of continuous fire before it overheats; 0 when it never does.
    double timeToOverheat() const;
    // Damage a second over a fire–overheat–recover or fire–empty–refill cycle,
    // with the capacitor refilling at `efficiency` of full power.
    double sustainedDps(double efficiency = 1) const;
};

struct MissileStats
{
    Damage damage;
    QString tracking; // "Infrared", "Electromagnetic", "CrossSection"
    double lockTime = 0, lockAngle = 0, lockRangeMin = 0, lockRangeMax = 0;
    double speed = 0, armTime = 0, lifetime = 0;
};

struct ShieldStats
{
    double hp = 0, regen = 0, damagedDelay = 0, downedDelay = 0;
    Damage resistanceMin, resistanceMax; // 0..1 of damage the shield shrugs off
    Damage absorptionMin, absorptionMax; // 0..1 of damage the shield takes; the rest reaches the hull
};

struct QuantumStats
{
    double speed = 0, splineSpeed = 0;           // m/s
    double spoolTime = 0, cooldown = 0;          // s
    double stageOneAccel = 0, stageTwoAccel = 0; // m/s²
    double fuelPerGm = 0;                        // SCU of quantum fuel a gigametre
};

struct ThrusterStats
{
    QString kind;      // "Main", "Retro", "Maneuver", "Vtol"
    double thrust = 0; // N
};

struct ArmorStats
{
    Damage multiplier{1, 1, 1, 1, 1, 1}; // of damage the hull takes
    Damage deflection;                   // a hit doing less than this per type glances off
    double emMultiplier = 1, irMultiplier = 1, csMultiplier = 1;
};

struct FlightStats
{
    double scmSpeed = 0, boostForward = 0, boostBackward = 0, maxSpeed = 0; // m/s
    double pitch = 0, yaw = 0, roll = 0;                                    // °/s
    double pitchBoost = 1, yawBoost = 1, rollBoost = 1;                     // boosted: × these
    double boostCapacity = 0, boostRegen = 0, boostRegenDelay = 0;          // afterburner capacitor
};

struct CountermeasureStats
{
    QString kind; // "Decoy" (flares) or "Noise" (chaff)
    int ammo = 0;
};

// A stock loadout entry. Records name the item twice, by class name and by
// reference, and the two disagree in a few hundred entries (one copied from
// another ship and left stale); Loadout picks per entry.
struct LoadoutEntry
{
    QString port;
    QString item;    // the entityClassName's item id; may be empty
    QString refItem; // the entityClassReference's item id; may be empty
    std::vector<LoadoutEntry> children;
};

struct Item
{
    QString id; // the record's class name: "POWR_LPLT_S01_PowerBolt_SCItem"
    QString guid;
    QString name, shortName, description;
    QString manufacturer, manufacturerCode; // "Lightning Power Ltd.", "LPLT"
    QString type, subType;                  // AttachDef: "PowerPlant", "Power"
    int size = 0, grade = 0;                // grade 1..4 shows as A..D
    QString itemClass;                      // "Civilian", "Military", ...: the description's Class line
    QStringList tags, requiredTags;
    double mass = 0, health = 0;
    bool named = false;                // has a real display name (not a placeholder)
    std::vector<Port> ports;           // what it holds (gimbals, racks, turrets)
    std::vector<LoadoutEntry> loadout; // what it comes with
    Resources resources;
    std::optional<WeaponStats> weapon;
    std::optional<MissileStats> missile;
    std::optional<ShieldStats> shield;
    std::optional<QuantumStats> quantum;
    std::optional<ThrusterStats> thruster;
    std::optional<ArmorStats> armor;
    std::optional<FlightStats> flight;
    std::optional<CountermeasureStats> countermeasure;
    double fuelCapacity = 0; // SCU: fuel and quantum fuel tanks

    QString gradeLetter() const;
};

struct HullPart
{
    QString name;
    double hp = 0;
    bool vital = false; // the hull's main sections: losing one loses the ship
};

struct PowerPool
{
    QString itemType; // "WeaponGun", "Shield", ...
    int size = 0;     // fixed pools: the pips the pool has
    bool fixed = false;
};

struct Ship
{
    QString id; // "AEGS_Avenger_Stalker"
    QString name, manufacturer, manufacturerCode, description, career, role;
    bool groundVehicle = false;
    int size = 0, crew = 0;
    double length = 0, width = 0, height = 0; // m
    double mass = 0;                          // kg, the hull without its items
    std::vector<HullPart> hull;
    std::vector<Port> ports;
    QStringList portTags;
    std::vector<LoadoutEntry> loadout;
    std::vector<PowerPool> powerPools;

    double hullHp() const;
    double vitalHp() const;
};

struct Catalog
{
    std::vector<Ship> ships; // by name
    std::vector<Item> items;
    QHash<QString, int> byId;   // lower-cased item id -> items index
    QHash<QString, int> byGuid; // item __ref -> items index
    bool hasVehicles = false;   // the cache has vehicle definitions (newer caches only)

    const Item *item(const QString &id) const;
    const Ship *ship(const QString &id) const;
};

struct CatalogSources
{
    QString recordsDir;          // dataForgeRecordsDir()
    QString vehiclesDir;         // dataForgeVehiclesDir()
    const IniMap *loc = nullptr; // the display language's base.ini
};

Catalog buildCatalog(const CatalogSources &sources, const std::atomic<bool> *cancel = nullptr);

// Whether `item` can go in `port` on a ship offering `shipTags`: its type and
// size, and the tags each side requires of the other.
bool fits(const Port &port, const Item &item, const QStringList &shipTags);

// The ship records players fly: not AI, template, tutorial or event copies.
bool isPlayerShipRecord(const QString &recordName);

} // namespace core::loadout
