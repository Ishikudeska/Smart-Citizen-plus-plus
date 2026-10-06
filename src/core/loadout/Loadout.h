#pragma once

#include "core/loadout/Catalog.h"

#include <QList>
#include <QPair>
#include <QString>

#include <vector>

// A ship's fitted items as a tree of slots: the ship's ports, each holding
// an item, whose own ports hold more (a gimbal's gun, a rack's missiles).
// Starts from the stock loadout; editable slots take any item that fits.
namespace core::loadout {

struct Slot
{
    Port port;
    QString path; // port names from the ship down: "hardpoint_weapon_class2_nose/hardpoint_class_2"
    const Item *item = nullptr; // what's fitted; null when empty
    std::vector<Slot> children; // the fitted item's ports
};

// The item a stock entry puts in `port` (null: none; `port` may be null for a
// port the ship doesn't list). When the entry's class name and reference name
// different items, the one that holds the entry's child entries wins, then
// the one that fits the port, then the class name.
const Item *stockItem(const Catalog &catalog, const QStringList &shipTags, const Port *port,
                      const LoadoutEntry &entry);

class Loadout
{
public:
    Loadout() = default;
    // The ship as it comes. `catalog` must outlive the loadout.
    Loadout(const Catalog &catalog, const Ship &ship);

    const Ship *ship() const { return ship_; }
    const Catalog *catalog() const { return catalog_; }
    const std::vector<Slot> &roots() const { return slots_; }
    const Slot *find(const QString &path) const;

    // Fits `itemId` (empty: empties the slot) in an editable slot when it fits;
    // the item's own ports get what it comes with.
    bool setItem(const QString &path, const QString &itemId);
    // Every named item that fits the slot, best first (largest, then grade, then name).
    std::vector<const Item *> compatible(const QString &path) const;

    // Every editable slot's item (path, item id; "" for empty), parents
    // first: what a saved loadout keeps. The rest comes with the ship.
    QList<QPair<QString, QString>> entries() const;
    // Back from entries(), on the stock loadout; slots that no longer exist or
    // items that no longer fit are skipped. Returns how many were skipped.
    int apply(const QList<QPair<QString, QString>> &entries);
    // Slots whose item differs from the stock loadout's.
    QList<QPair<QString, QString>> changes() const;

    // Every slot, depth first, with its depth.
    template <class Visit> void forEach(Visit &&visit) const // visit(const Slot &, int depth)
    {
        forEachIn(slots_, 0, visit);
    }

private:
    template <class Visit> static void forEachIn(const std::vector<Slot> &list, int depth, Visit &visit)
    {
        for (const Slot &s : list) {
            visit(s, depth);
            forEachIn(s.children, depth + 1, visit);
        }
    }
    Slot *findMutable(const QString &path);
    void fill(Slot &slot, const Item *item, const std::vector<LoadoutEntry> *overrides);

    const Catalog *catalog_ = nullptr;
    const Ship *ship_ = nullptr;
    std::vector<Slot> slots_;
};

} // namespace core::loadout
