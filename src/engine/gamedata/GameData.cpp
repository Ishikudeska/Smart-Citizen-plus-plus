#include "engine/gamedata/GameData.h"

#include "engine/forge/RecordBuilder.h"
#include "engine/gamedata/DotNet.h"
#include "engine/gamedata/Json.h"
#include "engine/io/RandomAccessFile.h"
#include "engine/xml/DotNetXmlWriter.h"
#include "engine/xml/XmlTree.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace engine::gamedata {

namespace {

using xml::XmlTree;
using NodeId = XmlTree::NodeId;
constexpr NodeId kNone = XmlTree::kNone;

// ── strings ───────────────────────────────────────────────────────────────

std::string utf8(std::string_view latin1)
{
    std::string out;
    xml::appendLatin1AsUtf8(out, latin1);
    return out;
}

std::filesystem::path fsPath(const std::string &utf8Path)
{
    return std::filesystem::path(std::u8string(utf8Path.begin(), utf8Path.end()));
}

std::string asciiLower(std::string_view s)
{
    std::string out(s);
    for (char &c : out)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + 32);
    return out;
}

bool startsWithIgnoreCase(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size() && asciiLower(s.substr(0, prefix.size())) == asciiLower(prefix);
}

bool startsWith(std::string_view s, std::string_view prefix)
{
    return s.substr(0, prefix.size()) == prefix;
}

bool endsWith(std::string_view s, std::string_view suffix)
{
    return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

std::vector<std::string> split(std::string_view s, char sep, bool removeEmpty)
{
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t at = s.find(sep, start);
        const std::string_view part = s.substr(start, at == std::string_view::npos ? std::string_view::npos : at - start);
        if (!removeEmpty || !part.empty())
            parts.emplace_back(part);
        if (at == std::string_view::npos)
            return parts;
        start = at + 1;
    }
}

std::string join(const std::vector<std::string> &parts, std::string_view sep, std::size_t from = 0)
{
    std::string out;
    for (std::size_t i = from; i < parts.size(); ++i) {
        if (i > from)
            out += sep;
        out += parts[i];
    }
    return out;
}

std::string replaceAll(std::string s, std::string_view from, std::string_view to)
{
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string_view trimChar(std::string_view s, char c)
{
    while (!s.empty() && s.front() == c)
        s.remove_prefix(1);
    while (!s.empty() && s.back() == c)
        s.remove_suffix(1);
    return s;
}

// ── navigating a record (System.Xml semantics) ───────────────────────────

struct Record
{
    XmlTree tree;
    NodeId root = kNone;
};

bool isElement(const XmlTree &t, NodeId n, std::string_view name)
{
    return t.kind(n) == XmlTree::Kind::Element && t.name(n) == name;
}

// XmlElement.GetElementsByTagName(name): descendants, document order.
// visit(NodeId) -> bool: false stops.
template <class Visit>
void forDescendants(const XmlTree &t, NodeId n, std::string_view name, Visit &&visit)
{
    NodeId cur = t.firstChild(n);
    std::vector<NodeId> parents{n};
    while (cur != kNone) {
        if (t.kind(cur) == XmlTree::Kind::Element) {
            if (t.name(cur) == name && !visit(cur))
                return;
            if (const NodeId first = t.firstChild(cur); first != kNone) {
                parents.push_back(cur);
                cur = first;
                continue;
            }
        }
        while (cur != kNone && t.nextSibling(cur) == kNone && parents.size() > 1) {
            cur = parents.back();
            parents.pop_back();
        }
        cur = cur == kNone ? kNone : t.nextSibling(cur);
    }
}

NodeId findFirst(const XmlTree &t, NodeId n, std::string_view name)
{
    if (n == kNone)
        return kNone;
    NodeId found = kNone;
    forDescendants(t, n, name, [&](NodeId d) {
        found = d;
        return false;
    });
    return found;
}

std::vector<NodeId> findAll(const XmlTree &t, NodeId n, std::string_view name)
{
    std::vector<NodeId> out;
    if (n != kNone)
        forDescendants(t, n, name, [&](NodeId d) {
            out.push_back(d);
            return true;
        });
    return out;
}

NodeId child(const XmlTree &t, NodeId n, std::string_view name)
{
    if (n == kNone)
        return kNone;
    for (NodeId c = t.firstChild(n); c != kNone; c = t.nextSibling(c))
        if (isElement(t, c, name))
            return c;
    return kNone;
}

std::optional<std::string> attr(const XmlTree &t, NodeId n, std::string_view name)
{
    if (n == kNone)
        return std::nullopt;
    if (const auto v = t.attribute(n, name))
        return utf8(*v);
    return std::nullopt;
}

double attrDouble(const XmlTree &t, NodeId n, std::string_view name, double fallback = 0.0)
{
    const auto s = attr(t, n, name);
    if (!s || s->empty())
        return fallback;
    return dotnet::parseDouble(*s).value_or(fallback);
}

std::optional<double> attrDoubleNullable(const XmlTree &t, NodeId n, std::string_view name)
{
    const auto s = attr(t, n, name);
    if (!s || s->empty())
        return std::nullopt;
    return dotnet::parseDouble(*s);
}

std::int32_t attrInt(const XmlTree &t, NodeId n, std::string_view name, std::int32_t fallback = 0)
{
    const auto s = attr(t, n, name);
    if (!s || s->empty())
        return fallback;
    return dotnet::parseInt32(*s).value_or(fallback);
}

std::optional<std::int32_t> attrIntNullable(const XmlTree &t, NodeId n, std::string_view name)
{
    const auto s = attr(t, n, name);
    if (!s || s->empty())
        return std::nullopt;
    return dotnet::parseInt32(*s);
}

std::string entityIdFromRoot(const XmlTree &t, NodeId root)
{
    const std::string name = utf8(t.name(root));
    const std::size_t dot = name.find('.');
    return dot == std::string::npos ? name : name.substr(dot + 1);
}

std::optional<double> roundOpt(std::optional<double> v, int decimals)
{
    if (!v)
        return std::nullopt;
    return dotnet::round(*v, decimals);
}

// The Localization child of the item's AttachDef, which names it.
std::optional<std::string> attachLocKey(const XmlTree &t, NodeId attach)
{
    if (const NodeId loc = child(t, attach, "Localization"); loc != kNone)
        return attr(t, loc, "Name");
    return std::nullopt;
}

// ── models (Models.cs) ────────────────────────────────────────────────────

struct Damage
{
    double phys = 0, energy = 0, dist = 0, therm = 0, bio = 0, stun = 0;
    bool allZero() const { return phys == 0 && energy == 0 && dist == 0 && therm == 0 && bio == 0 && stun == 0; }
};

struct Resistance
{
    double phys = 1, energy = 1, dist = 1, therm = 1, bio = 1, stun = 1;
};

Damage damageFrom(const XmlTree &t, NodeId n)
{
    return {attrDouble(t, n, "DamagePhysical"),   attrDouble(t, n, "DamageEnergy"),
            attrDouble(t, n, "DamageDistortion"), attrDouble(t, n, "DamageThermal"),
            attrDouble(t, n, "DamageBiochemical"), attrDouble(t, n, "DamageStun")};
}

struct Weapon
{
    std::string id, name;
    std::int32_t size = 0;
    std::string kind = "gun";
    Damage damage;
    std::optional<std::int32_t> pelletCount;
    std::optional<double> rateOfFire, projectileVelocity, projectileLifetime, range, alphaDamage, alphaPhys,
        alphaEnergy, alphaDist, dpsBurst;
    std::optional<std::int32_t> magazineCapacity;
    std::optional<double> basePenetrationDistance, heatCapacity, heatPerShot, coolingDelay, coolingPerSecond,
        overheatFixTime;
    std::optional<std::int32_t> shotsToOverheat;
    std::optional<double> timeToOverheat;
    std::optional<double> lockTime, lockRangeMin, lockRangeMax, armTime, health;
    std::optional<std::string> missileSubtype;
    std::optional<std::string> guid;
};

struct Armor
{
    std::string id, name;
    Damage deflection, multiplier;
    Resistance resistance;
    std::optional<double> hullHp;
    std::optional<std::string> guid;
};

struct Agility
{
    double pitch = 0, yaw = 0, roll = 0, pitchBoosted = 0, yawBoosted = 0, rollBoosted = 0;
};
struct Acceleration
{
    double main = 0, retro = 0, vtol = 0, maneuver = 0, mainBoosted = 0, maneuverBoosted = 0;
};
struct Thrust
{
    double main = 0, retro = 0, vtol = 0, maneuvering = 0;
};
struct CrossSection
{
    double x = 0, y = 0, z = 0;
};

struct Ifcs
{
    std::string id;
    double scmSpeed = 0, boostSpeedForward = 0, maxSpeed = 0;
    double angX = 0, angY = 0, angZ = 0;
    double multX = 1, multY = 1, multZ = 1;
    double linForward = 1;
    double mass = 0;
    std::optional<std::string> guid;
};

struct Thruster
{
    std::string id;
    double thrustCapacity = 0;
    std::string type;
    bool onlyVtol = false;
    double mass = 0;
    std::optional<std::string> guid;
};

struct Slot
{
    std::string label;
    std::int32_t size = 0;
    std::string kind = "gun";
    std::optional<std::string> stockWeaponId;
    std::optional<std::int32_t> minSize, maxSize;
    std::optional<std::string> stockRackId;
    std::optional<std::vector<std::optional<std::string>>> stockMissileIds;
};

struct Vehicle
{
    std::string id, name;
    std::optional<std::string> armorId;
    std::vector<Slot> slots;
    std::optional<Damage> deflection, multiplier;
    std::optional<Resistance> resistance;
    std::optional<double> length, width, height;
    std::optional<std::int32_t> crewSize;
    std::optional<std::string> career, role;
    std::optional<std::int32_t> sizeClass;
    std::optional<double> scmSpeed, boostSpeed, navSpeed, mass, massLoadout, massTotal, hullHp;
    std::optional<Agility> agility;
    std::optional<Acceleration> acceleration;
    std::optional<Thrust> thrustCapacity;
    std::optional<CrossSection> crossSection;
};

struct Rack
{
    std::string id;
    std::optional<std::string> guid;
    std::string name;
    std::int32_t size = 0, missileSize = 0, missileCount = 0;
    std::optional<std::string> manufacturer;
};

struct AmmoEntry
{
    Damage damage;
    std::optional<double> basePenetrationDistance, speed, lifetime;
};

using Loc = std::unordered_map<std::string, std::string>;

// ── NameResolver (XmlHelpers.cs) ──────────────────────────────────────────

bool isAllUpperAlpha(std::string_view s)
{
    return std::all_of(s.begin(), s.end(), [](char c) { return c >= 'A' && c <= 'Z'; });
}

bool isShipDesignator(const std::string &p)
{
    static const std::regex designator(R"(^[A-Z]+\d[A-Za-z0-9]*$)");
    static const std::regex weaponSize(R"(^S\d+$)");
    return std::regex_match(p, designator) && !std::regex_match(p, weaponSize);
}

std::string prettifyEntityId(std::string entityId, std::string_view typePrefix)
{
    static const char *const roman[] = {"", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"};
    if (startsWith(entityId, typePrefix))
        entityId = entityId.substr(typePrefix.size());
    if (const std::size_t us = entityId.find('_'); us != std::string::npos && us > 0) {
        const std::string head = entityId.substr(0, us), rest = entityId.substr(us + 1);
        if (head.size() == 4 && isAllUpperAlpha(head) && !rest.empty())
            entityId = rest;
    }
    std::string s(dotnet::trim(replaceAll(entityId, "_", " ")));
    std::vector<std::string> parts = split(s, ' ', false);
    if (parts.size() >= 3 && !isShipDesignator(parts[0])) {
        for (std::size_t i = 1; i + 1 < parts.size(); ++i) {
            if (isShipDesignator(parts[i])) {
                std::string p = parts[i];
                parts.erase(parts.begin() + static_cast<std::ptrdiff_t>(i));
                parts.insert(parts.begin(), p);
                break;
            }
        }
    }
    s = join(parts, " ");
    static const std::regex mk(R"(\bMk(\d+)\b)");
    std::string out;
    auto last = s.cbegin();
    for (std::sregex_iterator it(s.begin(), s.end(), mk), end; it != end; ++it) {
        out.append(last, (*it)[0].first);
        const std::string digits = (*it)[1].str();
        const auto n = dotnet::parseInt32(digits);
        if (n && *n >= 1 && *n <= 10)
            out += "Mk " + std::string(roman[*n]);
        else
            out += "Mk " + (n ? std::to_string(*n) : digits);
        last = (*it)[0].second;
    }
    out.append(last, s.cend());
    return out;
}

bool locKeyMatchesEntity(const std::string &key, const std::string &entityId)
{
    static const std::regex sizeClass(R"((?:^|_)S(\d+)(?:_|$))", std::regex::icase);
    std::smatch entSize, keySize;
    if (std::regex_search(entityId, entSize, sizeClass) && std::regex_search(key, keySize, sizeClass) &&
        entSize[1].str() != keySize[1].str())
        return false;
    const std::string keyLower = asciiLower(key);
    for (const std::string &token : split(entityId, '_', true)) {
        if (token.size() < 3)
            continue;
        if (std::regex_search("_" + token + "_", sizeClass))
            continue;
        if (keyLower.find(asciiLower(token)) != std::string::npos)
            return true;
    }
    return false;
}

bool isPlaceholder(std::string_view v)
{
    return v.empty() || startsWith(v, "<=") || startsWith(v, "@LOC_") || endsWith(v, "UNINITIALIZED");
}

std::string resolveName(const std::optional<std::string> &locKey, const Loc &loc, const std::string &fallback,
                        const std::string &entityId = {})
{
    if (!locKey || locKey->empty())
        return fallback;
    const std::string key = locKey->front() == '@' ? locKey->substr(1) : *locKey;
    if (!entityId.empty() && !locKeyMatchesEntity(key, entityId))
        return fallback;
    if (const auto it = loc.find(key); it != loc.end() && !isPlaceholder(it->second))
        return it->second;
    return fallback;
}

// ── the passes ────────────────────────────────────────────────────────────

constexpr std::string_view kRecords = "libs/foundry/records/";

class Extraction
{
public:
    Extraction(const RecordSource &source, const Loc &loc) : source_(source), loc_(loc)
    {
        for (std::size_t i = 0; i < source.paths.size(); ++i)
            paths_.push_back({source.paths[i], i});
    }

    // Every record whose path starts with `prefix` (ignoring case), in
    // PathToRecordMap order.
    template <class Visit>
    void forEach(std::string_view prefix, Visit &&visit)
    {
        Record rec;
        for (const auto &[path, index] : paths_) {
            if (!startsWithIgnoreCase(path, prefix))
                continue;
            rec.root = source_.build(index, rec.tree);
            if (rec.root == kNone)
                continue;
            visit(std::string_view(path).substr(prefix.size()), rec);
        }
    }

    std::unordered_map<std::string, AmmoEntry> ammo()
    {
        std::unordered_map<std::string, AmmoEntry> result; // keys lower-cased: OrdinalIgnoreCase
        forEach("libs/foundry/records/ammoparams/vehicle/", [&](std::string_view, const Record &r) {
            const XmlTree &t = r.tree;
            const auto guid = attr(t, r.root, "__ref");
            if (!guid || guid->empty())
                return;
            const NodeId damageInfo = findFirst(t, r.root, "DamageInfo");
            if (damageInfo == kNone)
                return;
            AmmoEntry e;
            e.damage = damageFrom(t, damageInfo);
            e.speed = attrDoubleNullable(t, r.root, "speed");
            e.lifetime = attrDoubleNullable(t, r.root, "lifetime");
            if (const NodeId bullet = findFirst(t, r.root, "BulletProjectileParams"); bullet != kNone)
                if (const NodeId pen = findFirst(t, bullet, "penetrationParams"); pen != kNone)
                    e.basePenetrationDistance = attrDoubleNullable(t, pen, "basePenetrationDistance");
            result[asciiLower(*guid)] = e;
        });
        return result;
    }

    void weapons(const std::unordered_map<std::string, AmmoEntry> &ammo, std::vector<Weapon> &guns,
                 std::vector<Weapon> &missiles)
    {
        constexpr std::string_view gunPrefix = "libs/foundry/records/entities/scitem/ships/weapons/";
        constexpr std::string_view missilePrefix = "libs/foundry/records/entities/scitem/ships/weapons/missiles/";
        Record rec;
        for (const auto &[path, index] : paths_) {
            const bool isMissile = startsWithIgnoreCase(path, missilePrefix);
            const bool isGun = !isMissile && startsWithIgnoreCase(path, gunPrefix);
            if (!isGun && !isMissile)
                continue;
            const std::string_view rest = std::string_view(path).substr(isMissile ? missilePrefix.size() : gunPrefix.size());
            if (rest.find('/') != std::string_view::npos) {
                if (isMissile)
                    continue;
                bool allowed = false;
                for (const std::string_view sub : {std::string_view("rocket_pods/"), std::string_view("emp/")})
                    if (startsWithIgnoreCase(rest, sub) && rest.substr(sub.size()).find('/') == std::string_view::npos) {
                        allowed = true;
                        break;
                    }
                if (!allowed)
                    continue;
            }
            rec.root = source_.build(index, rec.tree);
            if (rec.root == kNone)
                continue;
            if (isMissile) {
                if (auto m = parseMissile(rec))
                    missiles.push_back(std::move(*m));
            } else if (auto g = parseGun(rec, ammo)) {
                guns.push_back(std::move(*g));
            }
        }
    }

    std::vector<Armor> armors()
    {
        std::vector<Armor> result;
        forEach("libs/foundry/records/entities/scitem/ships/armor/", [&](std::string_view rest, const Record &r) {
            if (rest.find('/') != std::string_view::npos)
                return;
            const XmlTree &t = r.tree;
            const std::string entityId = entityIdFromRoot(t, r.root);
            const auto guid = attr(t, r.root, "__ref");
            const NodeId armor = findFirst(t, r.root, "SCItemVehicleArmorParams");
            if (armor == kNone)
                return;
            const NodeId deflectionWrap = findFirst(t, armor, "armorDeflection");
            const NodeId deflectionValue = deflectionWrap != kNone ? findFirst(t, deflectionWrap, "deflectionValue") : kNone;
            if (deflectionValue == kNone)
                return;
            const NodeId multiplierWrap = findFirst(t, armor, "damageMultiplier");
            const NodeId multiplierInfo = multiplierWrap != kNone ? findFirst(t, multiplierWrap, "DamageInfo") : kNone;

            Resistance res;
            if (const NodeId dr = findFirst(t, r.root, "DamageResistance"); dr != kNone) {
                const auto read = [&](std::string_view tag) {
                    const NodeId c = child(t, dr, tag);
                    return c == kNone ? 1.0 : attrDouble(t, c, "Multiplier", 1.0);
                };
                res = {read("PhysicalResistance"),  read("EnergyResistance"),      read("DistortionResistance"),
                       read("ThermalResistance"),   read("BiochemicalResistance"), read("StunResistance")};
            }

            const NodeId attach = findFirst(t, r.root, "AttachDef");
            const std::string fallback = prettifyEntityId(entityId, "ARMR_");
            std::string name = resolveName(attach != kNone ? attachLocKey(t, attach) : std::nullopt, loc_, fallback, entityId);
            if (endsWith(name, " Ship Armor"))
                name = std::string(dotnet::trimEnd(std::string_view(name).substr(0, name.size() - 11)));
            else if (endsWith(name, " Armor"))
                name = std::string(dotnet::trimEnd(std::string_view(name).substr(0, name.size() - 6)));
            if (name.empty())
                name = fallback;

            std::optional<double> hullHp;
            if (const NodeId health = findFirst(t, r.root, "SHealthComponentParams"); health != kNone)
                if (const double h = attrDouble(t, health, "Health", -1); h > 0)
                    hullHp = h;

            result.push_back({entityId, name, damageFrom(t, deflectionValue),
                              multiplierInfo != kNone ? damageFrom(t, multiplierInfo) : Damage{}, res, hullHp, guid});
        });
        return result;
    }

    std::vector<Ifcs> ifcs()
    {
        std::vector<Ifcs> result;
        forEach("libs/foundry/records/entities/scitem/ships/controller/", [&](std::string_view rest, const Record &r) {
            if (rest.find('/') != std::string_view::npos)
                return;
            const XmlTree &t = r.tree;
            const NodeId params = findFirst(t, r.root, "IFCSParams");
            if (params == kNone)
                return;
            Ifcs rec;
            rec.id = entityIdFromRoot(t, r.root);
            rec.guid = attr(t, r.root, "__ref");
            rec.scmSpeed = attrDouble(t, params, "scmSpeed");
            rec.boostSpeedForward = attrDouble(t, params, "boostSpeedForward");
            rec.maxSpeed = attrDouble(t, params, "maxSpeed");
            if (const NodeId sp = findFirst(t, params, "speedProfile"); sp != kNone)
                if (const NodeId av = findFirst(t, sp, "angularVelocity"); av != kNone) {
                    rec.angX = attrDouble(t, av, "x");
                    rec.angY = attrDouble(t, av, "y");
                    rec.angZ = attrDouble(t, av, "z");
                }
            if (const NodeId ab = findFirst(t, params, "afterburnerNew"); ab != kNone) {
                if (const NodeId m = findFirst(t, ab, "afterburnAngVelocityMultiplier"); m != kNone) {
                    rec.multX = attrDouble(t, m, "x", 1.0);
                    rec.multY = attrDouble(t, m, "y", 1.0);
                    rec.multZ = attrDouble(t, m, "z", 1.0);
                }
                if (const NodeId lin = findFirst(t, ab, "afterburnAccelMultiplierPositive"); lin != kNone)
                    rec.linForward = attrDouble(t, lin, "y", 1.0);
            }
            if (const NodeId attach = findFirst(t, r.root, "AttachDef"); attach != kNone)
                rec.mass = attrDouble(t, attach, "Mass");
            result.push_back(std::move(rec));
        });
        return result;
    }

    std::vector<Thruster> thrusters()
    {
        std::vector<Thruster> result;
        forEach("libs/foundry/records/entities/scitem/ships/thrusters/", [&](std::string_view rest, const Record &r) {
            if (rest.find('/') != std::string_view::npos)
                return;
            const XmlTree &t = r.tree;
            const NodeId params = findFirst(t, r.root, "SCItemThrusterParams");
            if (params == kNone)
                return;
            Thruster rec;
            rec.id = entityIdFromRoot(t, r.root);
            rec.guid = attr(t, r.root, "__ref");
            rec.thrustCapacity = attrDouble(t, params, "thrustCapacity");
            rec.type = attr(t, params, "thrusterType").value_or("");
            rec.onlyVtol = attrDouble(t, params, "onlyActiveInVTOL") > 0.5;
            if (const NodeId attach = findFirst(t, r.root, "AttachDef"); attach != kNone)
                rec.mass = attrDouble(t, attach, "Mass");
            result.push_back(std::move(rec));
        });
        return result;
    }

    std::vector<Rack> racks()
    {
        static const std::regex manufacturerPrefix(R"(^[A-Z]+_(?:S\d+_)?([A-Z]+)_)");
        std::vector<Rack> result;
        forEach("libs/foundry/records/entities/scitem/ships/missile_racks/", [&](std::string_view, const Record &r) {
            const XmlTree &t = r.tree;
            const std::string entityId = entityIdFromRoot(t, r.root);
            const auto guid = attr(t, r.root, "__ref");
            const NodeId attach = findFirst(t, r.root, "AttachDef");
            if (attach == kNone)
                return;
            if (attr(t, attach, "Type") != "MissileLauncher" || attr(t, attach, "SubType") != "MissileRack")
                return;
            const std::int32_t size = attrInt(t, attach, "Size");
            std::int32_t count = 0;
            std::optional<std::int32_t> missileSize;
            if (const NodeId ports = findFirst(t, r.root, "Ports"); ports != kNone) {
                for (NodeId port = t.firstChild(ports); port != kNone; port = t.nextSibling(port)) {
                    if (!isElement(t, port, "SItemPortDef"))
                        continue;
                    const NodeId types = child(t, port, "Types");
                    if (types == kNone)
                        continue;
                    bool missile = false;
                    for (NodeId c = t.firstChild(types); c != kNone; c = t.nextSibling(c))
                        if (isElement(t, c, "SItemPortDefTypes") && attr(t, c, "Type") == "Missile") {
                            missile = true;
                            break;
                        }
                    if (!missile)
                        continue;
                    ++count;
                    if (!missileSize)
                        missileSize = attrIntNullable(t, port, "MinSize");
                }
            }
            if (count == 0)
                return;
            const std::string fallback = prettifyEntityId(entityId, "");
            const std::string name = resolveName(attachLocKey(t, attach), loc_, fallback, entityId);
            std::optional<std::string> manufacturer;
            std::smatch m;
            if (std::regex_search(entityId, m, manufacturerPrefix))
                manufacturer = m[1].str();
            result.push_back({entityId, guid, name, size, missileSize.value_or(0), count, manufacturer});
        });
        std::sort(result.begin(), result.end(),
                  [](const Rack &a, const Rack &b) { return dotnet::compareOrdinal(a.id, b.id) < 0; });
        return result;
    }

    struct Indexes
    {
        std::unordered_map<std::string, const Weapon *> weaponByGuid, weaponById;
        std::unordered_map<std::string, const Armor *> armorById, armorByGuid;
        std::unordered_map<std::string, const Ifcs *> ifcsById, ifcsByGuid;
        std::unordered_map<std::string, const Thruster *> thrusterById, thrusterByGuid;
    };

    std::vector<Vehicle> vehicles(const Indexes &ix);

private:
    std::optional<Weapon> parseGun(const Record &r, const std::unordered_map<std::string, AmmoEntry> &ammoMap);
    std::optional<Weapon> parseMissile(const Record &r);

    const RecordSource &source_;
    const Loc &loc_;
    std::vector<std::pair<std::string, std::size_t>> paths_;
};

std::optional<std::int32_t> readPelletCount(const XmlTree &t, NodeId fireAct)
{
    const NodeId launch = findFirst(t, fireAct, "launchParams");
    if (launch == kNone)
        return std::nullopt;
    const NodeId sp = findFirst(t, launch, "SProjectileLauncher");
    if (sp == kNone)
        return std::nullopt;
    const std::int32_t n = attrInt(t, sp, "pelletCount", 0);
    return n > 1 ? std::optional(n) : std::nullopt;
}

std::optional<Weapon> Extraction::parseGun(const Record &r, const std::unordered_map<std::string, AmmoEntry> &ammoMap)
{
    const XmlTree &t = r.tree;
    Weapon w;
    w.id = entityIdFromRoot(t, r.root);
    w.guid = attr(t, r.root, "__ref");
    const NodeId container = findFirst(t, r.root, "SAmmoContainerComponentParams");
    if (container == kNone)
        return std::nullopt;
    const auto ammoGuid = attr(t, container, "ammoParamsRecord");
    if (!ammoGuid || ammoGuid->empty())
        return std::nullopt;
    const auto it = ammoMap.find(asciiLower(*ammoGuid));
    if (it == ammoMap.end())
        return std::nullopt;
    const AmmoEntry &ammo = it->second;
    if (ammo.damage.allZero())
        return std::nullopt;

    const NodeId attach = findFirst(t, r.root, "AttachDef");
    w.size = attrInt(t, attach, "Size", 0);
    w.name = resolveName(attach != kNone ? attachLocKey(t, attach) : std::nullopt, loc_, prettifyEntityId(w.id, ""), w.id);

    std::optional<double> fireRate, heatPerShot;
    std::optional<std::int32_t> pellets;
    for (const char *tag : {"SWeaponActionFireSingleParams", "SWeaponActionFireBurstParams"})
        for (const NodeId act : findAll(t, r.root, tag)) {
            if (!fireRate)
                fireRate = attrDoubleNullable(t, act, "fireRate");
            if (!heatPerShot)
                heatPerShot = attrDoubleNullable(t, act, "heatPerShot");
            if (!pellets)
                pellets = readPelletCount(t, act);
        }
    std::optional<std::int32_t> capacity;
    if (const std::int32_t cap = attrInt(t, container, "maxAmmoCount"); cap > 0)
        capacity = cap;

    std::optional<double> heatCapacity, coolingDelay, coolingPerSecond, overheatFixTime;
    bool overheat = false;
    if (const NodeId heat = findFirst(t, r.root, "SWeaponSimplifiedHeatParams"); heat != kNone) {
        overheat = true;
        heatCapacity = attrDoubleNullable(t, heat, "overheatTemperature");
        coolingPerSecond = attrDoubleNullable(t, heat, "coolingPerSecond");
        coolingDelay = attrDoubleNullable(t, heat, "timeTillCoolingStarts");
        overheatFixTime = attrDoubleNullable(t, heat, "overheatFixTime");
    }
    std::optional<std::int32_t> shots;
    std::optional<double> timeToOverheat;
    if (overheat && heatCapacity && *heatCapacity > 0 && heatPerShot && *heatPerShot > 0) {
        const double f = std::floor(*heatCapacity / *heatPerShot);
        // (Int32) of a double: saturating on .NET 9+, and nothing real gets near it.
        shots = f >= 2147483647.0 ? std::numeric_limits<std::int32_t>::max()
                : f <= -2147483648.0 ? std::numeric_limits<std::int32_t>::min()
                                     : static_cast<std::int32_t>(f);
        if (fireRate && *fireRate > 0)
            timeToOverheat = *shots / (*fireRate / 60.0);
    }
    const Damage &d = ammo.damage;
    const double alpha = d.phys + d.energy + d.dist + d.therm + d.bio + d.stun;
    std::optional<double> dpsBurst;
    if (fireRate && *fireRate > 0)
        dpsBurst = alpha * (*fireRate / 60.0);
    std::optional<double> range;
    if (ammo.speed && *ammo.speed > 0 && ammo.lifetime && *ammo.lifetime > 0)
        range = *ammo.speed * *ammo.lifetime;

    w.kind = "gun";
    w.damage = d;
    w.pelletCount = pellets && *pellets > 1 ? pellets : std::nullopt;
    w.rateOfFire = roundOpt(fireRate, 1);
    w.projectileVelocity = roundOpt(ammo.speed, 1);
    w.projectileLifetime = roundOpt(ammo.lifetime, 3);
    w.range = roundOpt(range, 0);
    w.alphaDamage = dotnet::round(alpha, 2);
    w.alphaPhys = dotnet::round(d.phys, 2);
    w.alphaEnergy = dotnet::round(d.energy, 2);
    w.alphaDist = dotnet::round(d.dist, 2);
    w.dpsBurst = roundOpt(dpsBurst, 1);
    w.magazineCapacity = capacity;
    w.basePenetrationDistance = roundOpt(ammo.basePenetrationDistance, 2);
    if (overheat) {
        w.heatCapacity = roundOpt(heatCapacity, 2);
        w.heatPerShot = roundOpt(heatPerShot, 3);
        w.coolingDelay = roundOpt(coolingDelay, 3);
        w.coolingPerSecond = roundOpt(coolingPerSecond, 2);
        w.overheatFixTime = roundOpt(overheatFixTime, 2);
        w.timeToOverheat = roundOpt(timeToOverheat, 3);
    }
    w.shotsToOverheat = shots;
    return w;
}

std::optional<Weapon> Extraction::parseMissile(const Record &r)
{
    const XmlTree &t = r.tree;
    Weapon w;
    w.id = entityIdFromRoot(t, r.root);
    w.guid = attr(t, r.root, "__ref");
    const NodeId params = findFirst(t, r.root, "SCItemMissileParams");
    if (params == kNone)
        return std::nullopt;
    NodeId damageElem = kNone;
    if (const NodeId explosion = findFirst(t, params, "explosionParams"); explosion != kNone)
        if (const NodeId wrap = findFirst(t, explosion, "damage"); wrap != kNone)
            damageElem = findFirst(t, wrap, "DamageInfo");
    if (damageElem == kNone)
        return std::nullopt;
    w.damage = damageFrom(t, damageElem);
    if (w.damage.allZero())
        return std::nullopt;

    const NodeId attach = findFirst(t, r.root, "AttachDef");
    w.size = attrInt(t, attach, "Size", 0);
    w.name = resolveName(attach != kNone ? attachLocKey(t, attach) : std::nullopt, loc_, prettifyEntityId(w.id, ""), w.id);

    std::optional<double> lockTime, lockMin, lockMax;
    if (const NodeId targeting = findFirst(t, params, "targetingParams"); targeting != kNone) {
        lockTime = attrDoubleNullable(t, targeting, "lockTime");
        lockMin = attrDoubleNullable(t, targeting, "lockRangeMin");
        lockMax = attrDoubleNullable(t, targeting, "lockRangeMax");
    }
    std::optional<double> linearSpeed;
    if (const NodeId gcs = findFirst(t, params, "GCSParams"); gcs != kNone)
        linearSpeed = attrDoubleNullable(t, gcs, "linearSpeed");
    const auto armTime = attrDoubleNullable(t, params, "armTime");
    const auto maxLifetime = attrDoubleNullable(t, params, "maxLifetime");
    std::optional<double> distance;
    if (linearSpeed && *linearSpeed > 0 && maxLifetime && *maxLifetime > 0)
        distance = *linearSpeed * *maxLifetime;

    std::optional<double> hp;
    if (const NodeId durability = findFirst(t, r.root, "Durability"); durability != kNone)
        hp = attrDoubleNullable(t, durability, "Health");
    else if (const NodeId health = findFirst(t, r.root, "SHealthComponentParams"); health != kNone)
        hp = attrDoubleNullable(t, health, "Health");

    w.kind = "missile";
    w.projectileVelocity = roundOpt(linearSpeed, 0);
    w.range = roundOpt(distance, 0);
    w.lockTime = roundOpt(lockTime, 2);
    w.lockRangeMin = roundOpt(lockMin, 0);
    w.lockRangeMax = roundOpt(lockMax, 0);
    w.armTime = roundOpt(armTime, 2);
    w.health = roundOpt(hp, 0);
    if (attach != kNone)
        w.missileSubtype = attr(t, attach, "SubType");
    return w;
}

// ── vehicles (VehicleExtractor.cs, PhysicsAggregator.cs) ─────────────────

const std::regex &nonPlayerIdRe()
{
    static const std::regex re("(^Orbital_Sentry"
                               "|_Hijacked(?:_|$)"
                               "|_PU_(Pirate|UEE|NineTails|Criminal|HOS)"
                               "|_Pirate$"
                               "|_Showdown$|_ShipShowdown$"
                               "|_BIS\\d+"
                               "|_Drug_"
                               "|_Unmanned$|_Crewless$|_Bombless$"
                               "|_Wreck(?:_|$)"
                               "|_NPC(?:_|$)|_AI(?:_|$)"
                               "|_Boarded$"
                               "|_Mission_PIR|_EA_PIR|_EA_Outlaws$"
                               "|_Collector_(Stealth|Military|Mod)"
                               "|_Exec_(Stealth|Military|StealthIndustrial)"
                               "|_Stealth$"
                               "|_S3Bombs$"
                               "|_FW22NFZ|_Fleetweek"
                               "|_Swarm$|_Civilian$"
                               "|_Arena_Commander|_Derelict|_Tutorial)");
    return re;
}

const std::regex &classRe()
{
    static const std::regex re(R"(class[_]?(\d+))", std::regex::icase);
    return re;
}

std::vector<NodeId> directEntries(const XmlTree &t, NodeId loadout)
{
    std::vector<NodeId> out;
    const NodeId entries = child(t, child(t, loadout, "SItemPortLoadoutManualParams"), "entries");
    if (entries == kNone)
        return out;
    for (NodeId c = t.firstChild(entries); c != kNone; c = t.nextSibling(c))
        if (isElement(t, c, "SItemPortLoadoutEntryParams"))
            out.push_back(c);
    return out;
}

void walkAllEntries(const XmlTree &t, NodeId loadout, std::vector<NodeId> &out)
{
    for (const NodeId entry : directEntries(t, loadout)) {
        out.push_back(entry);
        if (const NodeId nested = child(t, entry, "loadout"); nested != kNone)
            walkAllEntries(t, nested, out);
    }
}

struct WeaponHit
{
    std::vector<std::string> chain;
    const Weapon *weapon;
    std::optional<std::string> parentClassName;
};

template <class Map>
auto lookup(const Map &map, const std::optional<std::string> &key) -> typename Map::mapped_type
{
    if (!key || key->empty())
        return nullptr;
    const auto it = map.find(*key);
    return it == map.end() ? nullptr : it->second;
}

void walkForWeapons(const XmlTree &t, NodeId loadout, const std::vector<std::string> &chain,
                    const std::optional<std::string> &parentClassName, const Extraction::Indexes &ix,
                    std::vector<WeaponHit> &out)
{
    for (const NodeId entry : directEntries(t, loadout)) {
        std::vector<std::string> newChain = chain;
        newChain.push_back(attr(t, entry, "itemPortName").value_or(""));
        const auto refGuid = attr(t, entry, "entityClassReference");
        const auto cn = attr(t, entry, "entityClassName");
        const Weapon *weapon = lookup(ix.weaponByGuid, refGuid);
        if (!weapon)
            weapon = lookup(ix.weaponById, cn);
        if (const NodeId nested = child(t, entry, "loadout"); nested != kNone) {
            std::optional<std::string> containerId;
            if (cn && !cn->empty())
                containerId = cn;
            else if (refGuid && !refGuid->empty())
                containerId = "guid:" + *refGuid;
            walkForWeapons(t, nested, newChain, containerId, ix, out);
        }
        if (weapon)
            out.push_back({newChain, weapon, parentClassName});
    }
}

std::int32_t slotSizeFromChain(const std::vector<std::string> &chain)
{
    for (const std::string &port : chain) {
        std::smatch m;
        if (std::regex_search(port, m, classRe()))
            if (const auto v = dotnet::parseInt32(m[1].str()))
                return *v;
    }
    return 0;
}

std::string formatSlotLabel(const std::vector<std::string> &chain)
{
    static const std::regex missileAttach(R"(^missile +(?:attach +)?0*(\d+)( +attach)?$)", std::regex::icase);
    std::vector<std::string> cleaned;
    for (const std::string &port : chain) {
        std::string s = port;
        for (const std::string_view prefix : {"hardpoint_weapon_gun_", "hardpoint_weapon_", "hardpoint_turret_", "hardpoint_"})
            if (startsWith(s, prefix)) {
                s = s.substr(prefix.size());
                break;
            }
        s = std::regex_replace(s, classRe(), "");
        s = replaceAll(s, "__", "_");
        s = std::string(trimChar(s, '_'));
        std::replace(s.begin(), s.end(), '_', ' ');
        s = std::string(dotnet::trim(s));
        std::smatch m;
        if (std::regex_match(s, m, missileAttach))
            s = "#" + m[1].str();
        if (!s.empty())
            cleaned.push_back(s);
    }
    if (!cleaned.empty())
        return join(cleaned, " > ");
    return chain.empty() ? std::string() : chain.back();
}

std::string rackPrefix(const std::string &label)
{
    static const std::regex tubeSuffix(R"(^(.+)>\s*[^>]+$)");
    std::smatch m;
    if (std::regex_match(label, m, tubeSuffix))
        return std::string(dotnet::trim(m[1].str()));
    return label;
}

std::vector<Slot> collapseMissileRacks(const std::vector<Slot> &slots)
{
    std::vector<Slot> result;
    std::size_t i = 0;
    while (i < slots.size()) {
        const Slot &slot = slots[i];
        if (slot.kind != "missile") {
            result.push_back(slot);
            ++i;
            continue;
        }
        const std::string prefix = rackPrefix(slot.label);
        const auto rackId = slot.stockRackId;
        std::size_t j = i;
        Slot rack;
        rack.label = prefix;
        rack.kind = "missile_rack";
        rack.size = slot.size;
        rack.stockRackId = rackId;
        rack.stockMissileIds.emplace();
        std::int32_t lo = slot.size, hi = slot.size;
        while (j < slots.size() && slots[j].kind == "missile" && rackPrefix(slots[j].label) == prefix &&
               slots[j].stockRackId == rackId) {
            lo = std::min(lo, slots[j].size);
            hi = std::max(hi, slots[j].size);
            rack.stockMissileIds->push_back(slots[j].stockWeaponId);
            ++j;
        }
        rack.minSize = lo;
        rack.maxSize = hi;
        result.push_back(std::move(rack));
        i = j;
    }
    return result;
}

void aggregatePhysics(Vehicle &v, const XmlTree &t, NodeId root, const Ifcs *ifcs,
                      const std::vector<const Thruster *> &thrusters, const Armor *armor)
{
    if (const NodeId shipAttach = findFirst(t, root, "AttachDef"); shipAttach != kNone)
        if (const auto size = attr(t, shipAttach, "Size"))
            if (const auto parsed = dotnet::parseInt32(*size))
                v.sizeClass = *parsed;

    if (const NodeId vcp = findFirst(t, root, "VehicleComponentParams"); vcp != kNone)
        if (const NodeId mbb = findFirst(t, vcp, "maxBoundingBoxSize"); mbb != kNone)
            v.crossSection = CrossSection{attrDouble(t, mbb, "x"), attrDouble(t, mbb, "y"), attrDouble(t, mbb, "z")};

    double structuralMass = 0;
    if (const NodeId parts = findFirst(t, root, "Parts"); parts != kNone)
        for (const NodeId part : findAll(t, parts, "Part"))
            if (const double m = attrDouble(t, part, "mass"); m > 0)
                structuralMass += m;
    if (structuralMass > 0)
        v.mass = structuralMass;

    if (ifcs) {
        if (ifcs->scmSpeed > 0)
            v.scmSpeed = ifcs->scmSpeed;
        if (ifcs->boostSpeedForward > 0)
            v.boostSpeed = ifcs->boostSpeedForward;
        if (ifcs->maxSpeed > 0)
            v.navSpeed = ifcs->maxSpeed;
        v.agility = Agility{ifcs->angX, ifcs->angY, ifcs->angZ, ifcs->angX * ifcs->multX, ifcs->angY * ifcs->multY,
                            ifcs->angZ * ifcs->multZ};
    }

    Thrust buckets;
    double thrusterMass = 0;
    for (const Thruster *th : thrusters) {
        thrusterMass += th->mass;
        if (th->onlyVtol) {
            buckets.vtol += th->thrustCapacity;
            continue;
        }
        if (th->type == "Main")
            buckets.main += th->thrustCapacity;
        else if (th->type == "Retro")
            buckets.retro += th->thrustCapacity;
        else if (th->type == "Maneuver")
            buckets.maneuvering += th->thrustCapacity;
    }
    v.thrustCapacity = buckets;

    const double loadoutMass = thrusterMass + (ifcs ? ifcs->mass : 0);
    if (loadoutMass > 0)
        v.massLoadout = loadoutMass;
    if (v.mass || v.massLoadout)
        v.massTotal = v.mass.value_or(0) + v.massLoadout.value_or(0);

    if (v.massTotal && *v.massTotal > 0) {
        const double total = *v.massTotal;
        const double boost = ifcs ? ifcs->linForward : 1.0;
        v.acceleration = Acceleration{buckets.main / total,          buckets.retro / total,
                                      buckets.vtol / total,          buckets.maneuvering / total,
                                      (buckets.main / total) * boost, (buckets.maneuvering / total) * boost};
    }

    if (armor && armor->hullHp && *armor->hullHp > 0)
        v.hullHp = armor->hullHp;
}

std::vector<Vehicle> Extraction::vehicles(const Indexes &ix)
{
    constexpr std::string_view spaceships = "libs/foundry/records/entities/spaceships/";
    constexpr std::string_view groundVehicles = "libs/foundry/records/entities/groundvehicles/";
    std::vector<Vehicle> result;
    Record rec;
    for (const auto &[path, index] : paths_) {
        std::string_view prefix;
        if (startsWithIgnoreCase(path, spaceships))
            prefix = spaceships;
        else if (startsWithIgnoreCase(path, groundVehicles))
            prefix = groundVehicles;
        else
            continue;
        const bool isGround = prefix == groundVehicles;
        if (std::string_view(path).substr(prefix.size()).find('/') != std::string_view::npos)
            continue;
        rec.root = source_.build(index, rec.tree);
        if (rec.root == kNone)
            continue;
        const XmlTree &t = rec.tree;
        const NodeId root = rec.root;
        const std::string entityId = entityIdFromRoot(t, root);
        if (std::regex_search(entityId, nonPlayerIdRe()))
            continue;
        const NodeId loadoutParams = findFirst(t, root, "SEntityComponentDefaultLoadoutParams");
        const NodeId top = loadoutParams != kNone ? findFirst(t, loadoutParams, "loadout") : kNone;
        if (top == kNone)
            continue;

        std::optional<std::string> armorId;
        for (const NodeId entry : directEntries(t, top)) {
            const std::string port = attr(t, entry, "itemPortName").value_or("");
            if (port == "hardpoint_armor" || port == "hardpoint_armour") {
                const auto cn = attr(t, entry, "entityClassName");
                if (cn && !cn->empty()) {
                    armorId = cn;
                } else if (const Armor *a = lookup(ix.armorByGuid, attr(t, entry, "entityClassReference"))) {
                    armorId = a->id;
                }
                break;
            }
        }

        std::vector<WeaponHit> hits;
        walkForWeapons(t, top, {}, std::nullopt, ix, hits);
        std::vector<Slot> slots;
        std::unordered_set<std::string> seenChains;
        for (const WeaponHit &hit : hits) {
            if (!seenChains.insert(join(hit.chain, "/")).second)
                continue;
            Slot s;
            s.label = formatSlotLabel(hit.chain);
            s.size = hit.weapon->size != 0 ? hit.weapon->size : slotSizeFromChain(hit.chain);
            s.kind = hit.weapon->kind;
            s.stockWeaponId = hit.weapon->id;
            if (hit.weapon->kind == "missile")
                s.stockRackId = hit.parentClassName;
            slots.push_back(std::move(s));
        }
        std::vector<Slot> collapsed = collapseMissileRacks(slots);

        const Armor *armor = armorId && !armorId->empty() ? lookup(ix.armorById, armorId) : nullptr;
        if (!isGround && collapsed.empty() && !armor)
            continue;

        const NodeId attach = findFirst(t, root, "AttachDef");
        const std::string fallback = prettifyEntityId(entityId, "");
        std::string name = resolveName(attach != kNone ? attachLocKey(t, attach) : std::nullopt, loc_, fallback, entityId);
        const std::vector<std::string> words = split(name, ' ', true);
        if (words.size() > 1 && !fallback.empty() && !startsWith(fallback, words[0]))
            name = join(words, " ", 1);

        dotnet::introSort(collapsed, [](const Slot &a, const Slot &b) {
            if (b.size != a.size)
                return b.size < a.size ? -1 : 1;
            return dotnet::compareOrdinal(a.label, b.label);
        });

        Vehicle v;
        v.id = entityId;
        v.name = name;
        v.armorId = armorId;
        v.slots = std::move(collapsed);
        if (armor) {
            v.deflection = armor->deflection;
            v.multiplier = armor->multiplier;
            v.resistance = armor->resistance;
        }

        const Ifcs *ifcs = nullptr;
        std::vector<const Thruster *> thrusterList;
        std::vector<NodeId> all;
        walkAllEntries(t, top, all);
        for (const NodeId entry : all) {
            const auto refGuid = attr(t, entry, "entityClassReference");
            const auto cn = attr(t, entry, "entityClassName");
            if (!ifcs) {
                if (const Ifcs *byGuid = lookup(ix.ifcsByGuid, refGuid))
                    ifcs = byGuid;
                else if (const Ifcs *byId = lookup(ix.ifcsById, cn))
                    ifcs = byId;
            }
            if (const Thruster *byGuid = lookup(ix.thrusterByGuid, refGuid))
                thrusterList.push_back(byGuid);
            else if (const Thruster *byId = lookup(ix.thrusterById, cn))
                thrusterList.push_back(byId);
        }
        aggregatePhysics(v, t, root, ifcs, thrusterList, armor);

        if (const NodeId vc = findFirst(t, root, "VehicleComponentParams"); vc != kNone) {
            v.crewSize = attrIntNullable(t, vc, "crewSize");
            const auto career = attr(t, vc, "vehicleCareer");
            const auto role = attr(t, vc, "vehicleRole");
            if (career && !career->empty())
                v.career = resolveName(career, loc_, "");
            if (role && !role->empty())
                v.role = resolveName(role, loc_, "");
            if (const NodeId box = child(t, vc, "maxBoundingBoxSize"); box != kNone) {
                v.length = roundOpt(attrDoubleNullable(t, box, "y"), 2);
                v.width = roundOpt(attrDoubleNullable(t, box, "x"), 2);
                v.height = roundOpt(attrDoubleNullable(t, box, "z"), 2);
            }
        }
        result.push_back(std::move(v));
    }
    return result;
}

// ── overlay (OverlayApplier.cs) ───────────────────────────────────────────

std::optional<double> asDouble(const json::Value &v)
{
    if (v.type == json::Value::Type::Number && std::isfinite(v.number))
        return v.number;
    return std::nullopt;
}

// JsonSerializer.Deserialize<T> of a physics shape: missing members stay 0,
// a non-number member makes the whole value null.
template <class T, std::size_t N>
std::optional<T> deserialize(const json::Value &v, const std::array<std::pair<const char *, double T::*>, N> &members)
{
    if (v.type != json::Value::Type::Object)
        return std::nullopt;
    T out{};
    for (const auto &[name, member] : v.object) {
        for (const auto &[field, ptr] : members) {
            if (name != field)
                continue;
            if (member.type != json::Value::Type::Number)
                return std::nullopt;
            out.*ptr = member.number;
        }
    }
    return out;
}

int applyOverlay(const std::string &path, std::vector<Vehicle> &vehicles, const LogSink &log)
{
    const auto say = [&](const std::string &line) {
        if (log)
            log(line);
    };
    std::error_code ec;
    if (!std::filesystem::exists(fsPath(path), ec)) {
        say("WARN: overlay not found at " + path + " \xE2\x80\x94 skipping.");
        return 0;
    }
    auto file = io::RandomAccessFile::open(fsPath(path));
    if (!file)
        return 0;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file->size()));
    if (!file->readAt(0, bytes))
        return 0;
    const auto doc = json::parse(std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
    if (!doc)
        return 0;
    const json::Value *live = doc->find("vehicles");
    if (!live || live->type != json::Value::Type::Array)
        return 0;
    std::unordered_map<std::string, const json::Value *> byId;
    for (const json::Value &v : live->array)
        if (const json::Value *id = v.find("id"); id && id->type == json::Value::Type::String)
            byId[id->string] = &v;

    static constexpr std::array<std::pair<const char *, double Agility::*>, 6> agility{{
        {"pitch", &Agility::pitch}, {"yaw", &Agility::yaw}, {"roll", &Agility::roll},
        {"pitch_boosted", &Agility::pitchBoosted}, {"yaw_boosted", &Agility::yawBoosted},
        {"roll_boosted", &Agility::rollBoosted}}};
    static constexpr std::array<std::pair<const char *, double Acceleration::*>, 6> acceleration{{
        {"main", &Acceleration::main}, {"retro", &Acceleration::retro}, {"vtol", &Acceleration::vtol},
        {"maneuver", &Acceleration::maneuver}, {"main_boosted", &Acceleration::mainBoosted},
        {"maneuver_boosted", &Acceleration::maneuverBoosted}}};
    static constexpr std::array<std::pair<const char *, double Thrust::*>, 4> thrust{{
        {"main", &Thrust::main}, {"retro", &Thrust::retro}, {"vtol", &Thrust::vtol},
        {"maneuvering", &Thrust::maneuvering}}};
    static constexpr std::array<std::pair<const char *, double CrossSection::*>, 3> cross{{
        {"x", &CrossSection::x}, {"y", &CrossSection::y}, {"z", &CrossSection::z}}};

    int updated = 0, fills = 0;
    for (Vehicle &v : vehicles) {
        const auto it = byId.find(v.id);
        if (it == byId.end())
            continue;
        bool touched = false;
        const auto fill = [&](const char *field, auto &target, auto convert) {
            const json::Value *val = it->second->find(field);
            if (!val || val->type == json::Value::Type::Null || target)
                return;
            target = convert(*val);
            if (target) {
                touched = true;
                ++fills;
            }
        };
        fill("size_class", v.sizeClass, [](const json::Value &x) -> std::optional<std::int32_t> {
            if (x.type != json::Value::Type::Number)
                return std::nullopt;
            return dotnet::parseInt32(x.numberText);
        });
        fill("scm_speed", v.scmSpeed, asDouble);
        fill("boost_speed", v.boostSpeed, asDouble);
        fill("nav_speed", v.navSpeed, asDouble);
        fill("mass", v.mass, asDouble);
        fill("mass_loadout", v.massLoadout, asDouble);
        fill("mass_total", v.massTotal, asDouble);
        fill("hull_hp", v.hullHp, asDouble);
        fill("agility", v.agility, [](const json::Value &x) { return deserialize<Agility>(x, agility); });
        fill("acceleration", v.acceleration, [](const json::Value &x) { return deserialize<Acceleration>(x, acceleration); });
        fill("thrust_capacity", v.thrustCapacity, [](const json::Value &x) { return deserialize<Thrust>(x, thrust); });
        fill("cross_section", v.crossSection, [](const json::Value &x) { return deserialize<CrossSection>(x, cross); });
        if (touched)
            ++updated;
    }
    if (fills > 0)
        say("WARN: overlay backfilled " + std::to_string(fills) + " field(s) across " + std::to_string(updated) +
            " vehicle(s). Extraction couldn't resolve these \xE2\x80\x94 investigate per-ship coverage.");
    return updated;
}

// ── output ────────────────────────────────────────────────────────────────

void writeDamage(json::Writer &w, const char *name, const Damage &d)
{
    w.key(name);
    w.beginObject();
    w.field("phys", d.phys);
    w.field("energy", d.energy);
    w.field("dist", d.dist);
    w.field("therm", d.therm);
    w.field("bio", d.bio);
    w.field("stun", d.stun);
    w.endObject();
}

void writeResistance(json::Writer &w, const Resistance &r)
{
    w.key("resistance");
    w.beginObject();
    w.field("phys", r.phys);
    w.field("energy", r.energy);
    w.field("dist", r.dist);
    w.field("therm", r.therm);
    w.field("bio", r.bio);
    w.field("stun", r.stun);
    w.endObject();
}

void writeWeapon(json::Writer &w, const Weapon &x)
{
    w.beginObject();
    w.field("id", x.id);
    w.field("name", x.name);
    w.field("size", x.size);
    w.field("kind", x.kind);
    writeDamage(w, "damage", x.damage);
    w.field("pellet_count", x.pelletCount);
    w.field("rate_of_fire", x.rateOfFire);
    w.field("projectile_velocity", x.projectileVelocity);
    w.field("projectile_lifetime", x.projectileLifetime);
    w.field("range", x.range);
    w.field("alpha_damage", x.alphaDamage);
    w.field("alpha_phys", x.alphaPhys);
    w.field("alpha_energy", x.alphaEnergy);
    w.field("alpha_dist", x.alphaDist);
    w.field("dps_burst", x.dpsBurst);
    w.field("magazine_capacity", x.magazineCapacity);
    w.field("base_penetration_distance", x.basePenetrationDistance);
    w.field("heat_capacity", x.heatCapacity);
    w.field("heat_per_shot", x.heatPerShot);
    w.field("cooling_delay", x.coolingDelay);
    w.field("cooling_per_second", x.coolingPerSecond);
    w.field("overheat_fix_time", x.overheatFixTime);
    w.field("shots_to_overheat", x.shotsToOverheat);
    w.field("time_to_overheat", x.timeToOverheat);
    w.field("lock_time", x.lockTime);
    w.field("lock_range_min", x.lockRangeMin);
    w.field("lock_range_max", x.lockRangeMax);
    w.field("arm_time", x.armTime);
    w.field("health", x.health);
    w.field("missile_subtype", x.missileSubtype);
    w.endObject();
}

void writeArmor(json::Writer &w, const Armor &a)
{
    w.beginObject();
    w.field("id", a.id);
    w.field("name", a.name);
    writeDamage(w, "deflection", a.deflection);
    writeDamage(w, "multiplier", a.multiplier);
    writeResistance(w, a.resistance);
    w.field("hull_hp", a.hullHp);
    w.endObject();
}

void writeSlot(json::Writer &w, const Slot &s)
{
    w.beginObject();
    w.field("label", s.label);
    w.field("size", s.size);
    w.field("kind", s.kind);
    w.field("stock_weapon_id", s.stockWeaponId);
    w.field("min_size", s.minSize);
    w.field("max_size", s.maxSize);
    w.field("stock_rack_id", s.stockRackId);
    if (s.stockMissileIds) {
        w.key("stock_missile_ids");
        w.beginArray();
        for (const auto &id : *s.stockMissileIds) {
            if (id)
                w.value(*id);
            else
                w.null();
        }
        w.endArray();
    }
    w.endObject();
}

void writeVehicle(json::Writer &w, const Vehicle &v)
{
    w.beginObject();
    w.field("id", v.id);
    w.field("name", v.name);
    w.field("armor_id", v.armorId);
    w.key("slots");
    w.beginArray();
    for (const Slot &s : v.slots)
        writeSlot(w, s);
    w.endArray();
    if (v.deflection)
        writeDamage(w, "deflection", *v.deflection);
    if (v.multiplier)
        writeDamage(w, "multiplier", *v.multiplier);
    if (v.resistance)
        writeResistance(w, *v.resistance);
    w.field("length", v.length);
    w.field("width", v.width);
    w.field("height", v.height);
    w.field("crew_size", v.crewSize);
    w.field("career", v.career);
    w.field("role", v.role);
    w.field("size_class", v.sizeClass);
    w.field("scm_speed", v.scmSpeed);
    w.field("boost_speed", v.boostSpeed);
    w.field("nav_speed", v.navSpeed);
    w.field("mass", v.mass);
    w.field("mass_loadout", v.massLoadout);
    w.field("mass_total", v.massTotal);
    w.field("hull_hp", v.hullHp);
    if (v.agility) {
        w.key("agility");
        w.beginObject();
        w.field("pitch", v.agility->pitch);
        w.field("yaw", v.agility->yaw);
        w.field("roll", v.agility->roll);
        w.field("pitch_boosted", v.agility->pitchBoosted);
        w.field("yaw_boosted", v.agility->yawBoosted);
        w.field("roll_boosted", v.agility->rollBoosted);
        w.endObject();
    }
    if (v.acceleration) {
        w.key("acceleration");
        w.beginObject();
        w.field("main", v.acceleration->main);
        w.field("retro", v.acceleration->retro);
        w.field("vtol", v.acceleration->vtol);
        w.field("maneuver", v.acceleration->maneuver);
        w.field("main_boosted", v.acceleration->mainBoosted);
        w.field("maneuver_boosted", v.acceleration->maneuverBoosted);
        w.endObject();
    }
    if (v.thrustCapacity) {
        w.key("thrust_capacity");
        w.beginObject();
        w.field("main", v.thrustCapacity->main);
        w.field("retro", v.thrustCapacity->retro);
        w.field("vtol", v.thrustCapacity->vtol);
        w.field("maneuvering", v.thrustCapacity->maneuvering);
        w.endObject();
    }
    if (v.crossSection) {
        w.key("cross_section");
        w.beginObject();
        w.field("x", v.crossSection->x);
        w.field("y", v.crossSection->y);
        w.field("z", v.crossSection->z);
        w.endObject();
    }
    w.endObject();
}

void writeRack(json::Writer &w, const Rack &r)
{
    w.beginObject();
    w.field("id", r.id);
    w.field("name", r.name);
    w.field("size", r.size);
    w.field("missile_size", r.missileSize);
    w.field("missile_count", r.missileCount);
    w.field("manufacturer", r.manufacturer);
    w.endObject();
}

// File.ReadAllLines(path, UTF8) into the key=value map.
Loc loadLocalization(const std::string &path, const LogSink &log)
{
    Loc loc;
    if (path.empty())
        return loc;
    std::error_code ec;
    if (!std::filesystem::exists(fsPath(path), ec)) {
        if (log)
            log("WARN: base.ini not found at " + path +
                " \xE2\x80\x94 names will fall back to prettified entity ids.");
        return loc;
    }
    auto file = io::RandomAccessFile::open(fsPath(path));
    if (!file)
        return loc;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file->size()));
    if (!file->readAt(0, bytes))
        return loc;
    std::string_view text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    if (text.substr(0, 3) == "\xEF\xBB\xBF")
        text.remove_prefix(3);
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find_first_of("\r\n", pos);
        if (end == std::string_view::npos)
            end = text.size();
        const std::string_view line = text.substr(pos, end - pos);
        pos = end;
        if (pos < text.size() && text[pos] == '\r')
            ++pos;
        if (end < text.size() && text[end] == '\r' && pos < text.size() && text[pos] == '\n')
            ++pos;
        else if (end < text.size() && text[end] == '\n')
            ++pos;
        const std::size_t eq = line.find('=');
        if (eq == std::string_view::npos || eq == 0)
            continue;
        loc[std::string(line.substr(0, eq))] = std::string(line.substr(eq + 1));
    }
    return loc;
}

} // namespace

Result<std::string> buildGameDataJson(const forge::DataForge &forge, const Options &options, const LogSink &log)
{
    RecordSource source;
    source.dcbVersion = forge.version();
    std::vector<std::uint32_t> indexes;
    for (const auto index : forge.fileRecords()) {
        source.paths.push_back(utf8(forge.recordFileName(index)));
        indexes.push_back(index);
    }
    auto builder = std::make_shared<forge::RecordBuilder>(forge);
    source.build = [builder, indexes](std::size_t i, XmlTree &tree) { return builder->build(indexes[i], tree); };
    return buildGameDataJson(source, options, log);
}

Result<std::string> buildGameDataJson(const RecordSource &records, const Options &options, const LogSink &log)
{
    const auto say = [&](const std::string &line) {
        if (log)
            log(line);
    };
    const Loc loc = loadLocalization(options.baseIniPath, log);
    say("Localization keys: " + std::to_string(loc.size()));

    Extraction ex(records, loc);
    const auto ammo = ex.ammo();
    say("Ammo records: " + std::to_string(ammo.size()));
    std::vector<Weapon> guns, missiles;
    ex.weapons(ammo, guns, missiles);
    say("Weapons: " + std::to_string(guns.size() + missiles.size()) + " (" + std::to_string(guns.size()) +
        " guns + " + std::to_string(missiles.size()) + " missiles)");
    const std::vector<Armor> armors = ex.armors();
    say("Armor records: " + std::to_string(armors.size()));
    const std::vector<Ifcs> ifcs = ex.ifcs();
    say("IFCS controllers: " + std::to_string(ifcs.size()));
    const std::vector<Thruster> thrusters = ex.thrusters();
    say("Thrusters: " + std::to_string(thrusters.size()));

    std::vector<Weapon> weapons = guns;
    weapons.insert(weapons.end(), missiles.begin(), missiles.end());

    // ToDictionary throws on a duplicate key; the first wins here instead.
    Extraction::Indexes ix;
    for (const Weapon &w : weapons) {
        if (w.guid)
            ix.weaponByGuid.try_emplace(*w.guid, &w);
        ix.weaponById.try_emplace(w.id, &w);
    }
    for (const Armor &a : armors) {
        ix.armorById.try_emplace(a.id, &a);
        if (a.guid)
            ix.armorByGuid.try_emplace(*a.guid, &a);
    }
    for (const Ifcs &i : ifcs) {
        ix.ifcsById.try_emplace(i.id, &i);
        if (i.guid)
            ix.ifcsByGuid.try_emplace(*i.guid, &i);
    }
    for (const Thruster &t : thrusters) {
        ix.thrusterById.try_emplace(t.id, &t);
        if (t.guid)
            ix.thrusterByGuid.try_emplace(*t.guid, &t);
    }

    std::vector<Vehicle> vehicles = ex.vehicles(ix);
    std::size_t withSlots = 0, withPhysics = 0;
    for (const Vehicle &v : vehicles) {
        withSlots += !v.slots.empty();
        withPhysics += v.sizeClass.has_value();
    }
    say("Vehicles: " + std::to_string(vehicles.size()) + " (" + std::to_string(withSlots) +
        " with weapon hardpoints, " + std::to_string(withPhysics) + " with physics extracted)");

    const std::vector<Rack> racks = ex.racks();
    say("Missile racks: " + std::to_string(racks.size()));

    // Reconcile each missile bay's parent against the racks catalog.
    {
        std::unordered_set<std::string> rackIds;
        std::unordered_map<std::string, std::string> rackByGuid; // lower-cased guid -> first id
        for (const Rack &r : racks) {
            rackIds.insert(r.id);
            if (r.guid && !r.guid->empty())
                rackByGuid.try_emplace(asciiLower(*r.guid), r.id);
        }
        int rackSlots = 0, captured = 0, byClass = 0, byGuid = 0;
        for (Vehicle &v : vehicles)
            for (Slot &s : v.slots) {
                if (s.kind != "missile_rack")
                    continue;
                ++rackSlots;
                if (!s.stockRackId || s.stockRackId->empty())
                    continue;
                ++captured;
                const std::string raw = *s.stockRackId;
                if (startsWith(raw, "guid:")) {
                    if (const auto it = rackByGuid.find(asciiLower(raw.substr(5))); it != rackByGuid.end()) {
                        s.stockRackId = it->second;
                        ++byGuid;
                    } else {
                        s.stockRackId.reset();
                    }
                } else if (rackIds.contains(raw)) {
                    ++byClass;
                } else {
                    s.stockRackId.reset();
                }
            }
        const int resolved = byClass + byGuid;
        say("Missile-rack ids: " + std::to_string(resolved) + "/" + std::to_string(rackSlots) + " resolved (" +
            std::to_string(byClass) + " by class, " + std::to_string(byGuid) + " by guid; captured " +
            std::to_string(captured) + ", dropped " + std::to_string(captured - resolved) + " unresolved)");
    }

    if (!options.overlayPath.empty()) {
        const int overlaid = applyOverlay(options.overlayPath, vehicles, log);
        say("Overlay applied: " + std::to_string(overlaid) + " vehicles updated from " + options.overlayPath);
    }

    // LINQ OrderBy/ThenBy: stable, culture-aware names.
    std::stable_sort(weapons.begin(), weapons.end(), [](const Weapon &a, const Weapon &b) {
        if (a.size != b.size)
            return a.size < b.size;
        return dotnet::compareCulture(a.name, b.name) < 0;
    });
    std::vector<Armor> ships = armors;
    std::stable_sort(ships.begin(), ships.end(),
                     [](const Armor &a, const Armor &b) { return dotnet::compareCulture(a.name, b.name) < 0; });
    std::stable_sort(vehicles.begin(), vehicles.end(),
                     [](const Vehicle &a, const Vehicle &b) { return dotnet::compareCulture(a.name, b.name) < 0; });

    json::Writer w;
    w.beginObject();
    w.field("schema_version", std::int32_t{1});
    w.field("generated_at", options.generatedAt.empty() ? dotnet::utcNowRoundTrip() : options.generatedAt);
    w.field("channel", options.channel);
    w.field("dcb_version", std::int32_t{records.dcbVersion});
    w.key("weapons");
    w.beginArray();
    for (const Weapon &x : weapons)
        writeWeapon(w, x);
    w.endArray();
    w.key("ships");
    w.beginArray();
    for (const Armor &a : ships)
        writeArmor(w, a);
    w.endArray();
    w.key("vehicles");
    w.beginArray();
    for (const Vehicle &v : vehicles)
        writeVehicle(w, v);
    w.endArray();
    w.key("racks");
    w.beginArray();
    for (const Rack &r : racks)
        writeRack(w, r);
    w.endArray();
    w.endObject();
    return w.text() + "\r\n";
}

} // namespace engine::gamedata
