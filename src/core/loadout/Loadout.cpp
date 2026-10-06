#include "core/loadout/Loadout.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>

namespace core::loadout {

namespace {

const LoadoutEntry *entryFor(const std::vector<LoadoutEntry> *entries, const QString &port)
{
    if (!entries)
        return nullptr;
    for (const LoadoutEntry &e : *entries)
        if (e.port.compare(port, Qt::CaseInsensitive) == 0)
            return &e;
    return nullptr;
}

QString join(const QString &parent, const QString &name)
{
    return parent.isEmpty() ? name : parent + u'/' + name;
}

} // namespace

const Item *stockItem(const Catalog &catalog, const QStringList &shipTags, const Port *port,
                      const LoadoutEntry &entry)
{
    const Item *byName = entry.item.isEmpty() ? nullptr : catalog.item(entry.item);
    const Item *byRef = entry.refItem.isEmpty() ? nullptr : catalog.item(entry.refItem);
    if (!byName || !byRef || byName == byRef)
        return byName ? byName : byRef;
    const auto score = [&](const Item *item) {
        int held = 0;
        for (const LoadoutEntry &child : entry.children)
            held += std::any_of(item->ports.begin(), item->ports.end(), [&](const Port &p) {
                return p.name.compare(child.port, Qt::CaseInsensitive) == 0;
            });
        return held * 2 + (port && fits(*port, *item, shipTags) ? 1 : 0);
    };
    return score(byRef) > score(byName) ? byRef : byName;
}

Loadout::Loadout(const Catalog &catalog, const Ship &ship) : catalog_(&catalog), ship_(&ship)
{
    for (const Port &port : ship.ports) {
        Slot slot;
        slot.port = port;
        slot.path = port.name;
        const LoadoutEntry *entry = entryFor(&ship.loadout, port.name);
        fill(slot, entry ? stockItem(catalog, ship.portTags, &port, *entry) : nullptr,
             entry ? &entry->children : nullptr);
        slots_.push_back(std::move(slot));
    }
    // Stock items on ports the vehicle XML doesn't list (controllers, armor
    // on some ships) still count; they can't be swapped.
    for (const LoadoutEntry &entry : ship.loadout) {
        const bool listed = std::any_of(ship.ports.begin(), ship.ports.end(), [&](const Port &p) {
            return p.name.compare(entry.port, Qt::CaseInsensitive) == 0;
        });
        const Item *item = stockItem(catalog, ship.portTags, nullptr, entry);
        if (listed || !item)
            continue;
        Slot slot;
        slot.port.name = entry.port;
        slot.port.editable = false;
        slot.port.hidden = true;
        slot.path = entry.port;
        fill(slot, item, &entry.children);
        slots_.push_back(std::move(slot));
    }
}

void Loadout::fill(Slot &slot, const Item *item, const std::vector<LoadoutEntry> *overrides)
{
    slot.item = item;
    slot.children.clear();
    if (!item)
        return;
    for (const Port &port : item->ports) {
        Slot child;
        child.port = port;
        child.path = join(slot.path, port.name);
        // The ship's loadout names what goes in the item's ports; failing
        // that, the item comes with its own.
        const LoadoutEntry *entry = entryFor(overrides, port.name);
        const std::vector<LoadoutEntry> *grand = nullptr;
        if (entry) {
            grand = &entry->children;
        } else if ((entry = entryFor(&item->loadout, port.name))) {
            grand = &entry->children;
        }
        fill(child, entry ? stockItem(*catalog_, ship_->portTags, &port, *entry) : nullptr, grand);
        slot.children.push_back(std::move(child));
    }
}

const Slot *Loadout::find(const QString &path) const
{
    return const_cast<Loadout *>(this)->findMutable(path);
}

Slot *Loadout::findMutable(const QString &path)
{
    std::vector<Slot> *level = &slots_;
    Slot *found = nullptr;
    const QList<QStringView> names = QStringView(path).split(u'/');
    for (const QStringView name : names) {
        found = nullptr;
        for (Slot &s : *level)
            if (QStringView(s.port.name).compare(name, Qt::CaseInsensitive) == 0) {
                found = &s;
                break;
            }
        if (!found)
            return nullptr;
        level = &found->children;
    }
    return found;
}

bool Loadout::setItem(const QString &path, const QString &itemId)
{
    Slot *slot = findMutable(path);
    if (!slot || !slot->port.editable)
        return false;
    if (itemId.isEmpty()) {
        fill(*slot, nullptr, nullptr);
        return true;
    }
    const Item *item = catalog_->item(itemId);
    if (!item || !fits(slot->port, *item, ship_->portTags))
        return false;
    fill(*slot, item, nullptr);
    return true;
}

std::vector<const Item *> Loadout::compatible(const QString &path) const
{
    std::vector<const Item *> out;
    const Slot *slot = find(path);
    if (!slot)
        return out;
    // Placeholder and test records, and copies of an item under its name
    // (turret or mission versions): the base record stands for them, unless
    // a copy is what's fitted.
    static const QRegularExpression notForSale(
        QStringLiteral("(^|_)(temp|test|template|dummy|placeholder)(_|$)"),
        QRegularExpression::CaseInsensitiveOption);
    QHash<QString, const Item *> byName;
    for (const Item &item : catalog_->items) {
        const bool fitted = &item == slot->item;
        if (!fitted && (!item.named || notForSale.match(item.id).hasMatch()))
            continue;
        // Turrets: generic mounts ("gimbalMount", "turretMount", "fixedMount")
        // and ones a tag ties to this ship or port. The rest are other ships'
        // own turrets, which nothing stops on paper.
        if (!fitted && (item.type == u"Turret" || item.type == u"TurretBase")) {
            const bool mount = std::any_of(item.tags.begin(), item.tags.end(), [](const QString &t) {
                return t.compare(u"gimbalMount", Qt::CaseInsensitive) == 0 ||
                       t.compare(u"turretMount", Qt::CaseInsensitive) == 0 ||
                       t.compare(u"fixedMount", Qt::CaseInsensitive) == 0;
            });
            if (!mount && item.requiredTags.isEmpty() && slot->port.requiredTags.isEmpty())
                continue;
        }
        if (!fits(slot->port, item, ship_->portTags) && !fitted)
            continue;
        const QString key = item.name.toLower() + u'|' + QString::number(item.size);
        const Item *&kept = byName[key];
        if (!kept || fitted || (kept != slot->item && item.id.size() < kept->id.size()))
            kept = &item;
    }
    for (const Item *item : std::as_const(byName))
        out.push_back(item);
    std::stable_sort(out.begin(), out.end(), [](const Item *a, const Item *b) {
        if (a->size != b->size)
            return a->size > b->size;
        if (a->grade != b->grade)
            return a->grade < b->grade; // A before D
        if (const int c = a->name.localeAwareCompare(b->name); c != 0)
            return c < 0;
        return a->id < b->id;
    });
    return out;
}

QList<QPair<QString, QString>> Loadout::entries() const
{
    QList<QPair<QString, QString>> out;
    forEach([&](const Slot &s, int) {
        if (s.port.editable)
            out.append({s.path, s.item ? s.item->id : QString()});
    });
    return out;
}

int Loadout::apply(const QList<QPair<QString, QString>> &entries)
{
    int skipped = 0;
    // Parents first, so a child's entry lands in the item its parent got.
    QList<QPair<QString, QString>> sorted = entries;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const auto &a, const auto &b) { return a.first.count(u'/') < b.first.count(u'/'); });
    for (const auto &[path, id] : std::as_const(sorted)) {
        const Slot *slot = find(path);
        if (!slot) {
            ++skipped;
            continue;
        }
        const QString current = slot->item ? slot->item->id : QString();
        if (current.compare(id, Qt::CaseInsensitive) == 0)
            continue;
        if (!setItem(path, id))
            ++skipped;
    }
    return skipped;
}

QList<QPair<QString, QString>> Loadout::changes() const
{
    QList<QPair<QString, QString>> out;
    if (!ship_)
        return out;
    const Loadout stock(*catalog_, *ship_);
    forEach([&](const Slot &s, int) {
        const Slot *base = stock.find(s.path);
        const QString mine = s.item ? s.item->id : QString();
        const QString theirs = base && base->item ? base->item->id : QString();
        if (!base || mine != theirs)
            out.append({s.path, mine});
    });
    return out;
}

} // namespace core::loadout
