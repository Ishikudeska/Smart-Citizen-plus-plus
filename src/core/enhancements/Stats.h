#pragma once

#include "core/enhancements/Common.h"
#include "core/enhancements/Tags.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

// The "--- STATS ---" blocks for components, missiles, weapons and ships,
// read from DataForge records. Each returns the block (lines joined by the
// literal "\n"), "" for no stats; a PyError is the Python raising, which
// makes the scan skip that record.
namespace core::enh {

// Raised only through raise(): copying it is private, so it cannot be
// sliced, yet the throw (which needs that copy constructor) is allowed from
// inside the class. Handlers catch it by reference.
class PyError final : public std::runtime_error
{
public:
    [[noreturn]] static void raise(const char *message) { throw PyError(message); }
    PyError &operator=(const PyError &) = delete;

private:
    explicit PyError(const char *message) : std::runtime_error(message) {}
    PyError(const PyError &) = default;
};

// float(text), raising like Python on anything else.
double requireFloat(const std::optional<std::string_view> &value);

// The first attribute in `names` with a non-empty value (an `a or b or c`).
std::optional<std::string_view> firstSet(Node el, std::initializer_list<const char *> names);

std::optional<std::string_view> resourceAmount(Node amount);
std::optional<std::string_view> findResource(Node root, std::string_view resource);
std::optional<double> fireRate(Node root);
QStringList fireModes(Node root, const Loc *loc);

struct DamageBreakdown
{
    double total = 0;
    std::vector<std::pair<QString, double>> parts; // label -> amount, first-seen order
};
DamageBreakdown ammoDamageBreakdown(Node ammoRoot);

QString enhancementsShield(Node root);
QString enhancementsMissile(Node root);
QString enhancementsBombRack(Node root);
QString enhancementsRadar(Node root);
QString enhancementsCooler(Node root);
QString enhancementsPowerplant(Node root);
QString enhancementsQuantumDrive(Node root);
QString enhancementsMiningLaser(Node root);
QString enhancementsSalvageTool(Node root);

// magazine entity class -> (ammoParamsRecord, maxAmmoCount).
using MagazineLookup = QHash<QString, std::pair<QString, QString>>;
// `magazines` is non-null on the FPS path only.
QString enhancementsWeapon(Node root, const RecordLookup &ammo, const Loc *loc,
                           const MagazineLookup *magazines);

std::pair<QString, QString> loadoutSummary(Node root);
QString armorStatsBlock(Node armorRoot);
QString enhancementsShip(Node root, Node controllerRoot, const Loc *loc, const RecordLookup *armor);

} // namespace core::enh
