#pragma once

#include "core/loadout/Catalog.h"
#include "core/loadout/Loadout.h"
#include "core/loadout/Performance.h"

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

class AppController;

// The Loadout page: pick a ship, see its stock loadout and what it adds up
// to, swap items for ones that fit, split the power, save loadouts. The
// catalog is built off the GUI thread the first time the page shows, and
// again when the game data or the language changes. Figures reach QML
// formatted.
class LoadoutController : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    // "nodata" (no DataForge cache), "loading", "ready" or "idle".
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // The cache has the vehicle definitions (caches from before the loadouts don't).
    Q_PROPERTY(bool hasVehicles READ hasVehicles NOTIFY catalogChanged)
    // [{id, name, manufacturer, size, career, role, ground}], by name.
    Q_PROPERTY(QVariantList ships READ ships NOTIFY catalogChanged)
    Q_PROPERTY(QString shipId READ shipId WRITE setShipId NOTIFY loadoutChanged)
    Q_PROPERTY(QVariantMap ship READ ship NOTIFY loadoutChanged)
    // [{key, title, cards: [card]}]; a card: {path, rows: [row]}, its slot
    // first and then what the item holds; a row: {path, label, size, editable,
    // depth, item: {id, name, maker, size, grade, cls, stats: [{label, value}]}
    // or null}.
    Q_PROPERTY(QVariantList sections READ sections NOTIFY loadoutChanged)
    Q_PROPERTY(QVariantMap totals READ totals NOTIFY loadoutChanged)
    // [{key, label, category, min, max, pips}].
    Q_PROPERTY(QVariantList power READ power NOTIFY loadoutChanged)
    Q_PROPERTY(bool nav READ nav WRITE setNav NOTIFY loadoutChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY loadoutChanged)
    // Names of this ship's saved loadouts.
    Q_PROPERTY(QStringList saved READ saved NOTIFY savedChanged)

public:
    explicit LoadoutController(QObject *parent = nullptr);

    bool active() const { return active_; }
    void setActive(bool active);
    QString status() const { return status_; }
    bool hasVehicles() const { return catalog_ && catalog_->hasVehicles; }
    QVariantList ships() const { return ships_; }
    QString shipId() const;
    void setShipId(const QString &id);
    QVariantMap ship() const { return shipInfo_; }
    QVariantList sections() const { return sections_; }
    QVariantMap totals() const { return totals_; }
    QVariantList power() const { return power_; }
    bool nav() const { return plan_.nav; }
    void setNav(bool nav);
    bool modified() const { return modified_; }
    QStringList saved() const;

    // Items that fit the slot: [{id, name, maker, size, grade, cls, summary, current}].
    Q_INVOKABLE QVariantList compatibleItems(const QString &path) const;
    Q_INVOKABLE void setItem(const QString &path, const QString &itemId);
    Q_INVOKABLE void resetLoadout();
    Q_INVOKABLE void setPips(const QString &key, int pips);
    Q_INVOKABLE void resetPower();
    Q_INVOKABLE void saveLoadout(const QString &name);
    Q_INVOKABLE void loadLoadout(const QString &name);
    Q_INVOKABLE void deleteLoadout(const QString &name);
    // Quantum travel time over `gigametres`, spool included: "2 min 5 s".
    Q_INVOKABLE QString travelTime(double gigametres) const;
    // This loadout's sustained fire against another ship's stock loadout:
    // {target, shieldTime, killTime, deflected}.
    Q_INVOKABLE QVariantMap engage(const QString &targetShipId) const;
    Q_INVOKABLE void reload();
    // Re-extracts the DataForge cache (for a missing or pre-loadout one).
    Q_INVOKABLE void extractGameData();

signals:
    void activeChanged();
    void statusChanged();
    void catalogChanged();
    void loadoutChanged();
    void savedChanged();

private:
    struct SavedLoadout
    {
        QString name, ship;
        QList<QPair<QString, QString>> items;
        QHash<QString, int> pips;
        bool nav = false;
    };

    AppController &app() const;
    QString sourceKey() const;
    void sourcesChanged();
    void build();
    void setStatus(const QString &status);
    void selectShip(const core::loadout::Ship *ship);
    void refresh();
    QVariantMap card(const core::loadout::Slot &slot) const;
    QVariantMap itemMap(const core::loadout::Item &item) const;
    QVariantList statsOf(const core::loadout::Item &item) const;
    void loadSaved();
    void writeSaved() const;
    QString savedFile() const;

    std::shared_ptr<const core::loadout::Catalog> catalog_;
    core::loadout::Loadout loadout_;
    core::loadout::PowerPlan plan_;
    core::loadout::Totals current_;
    QVariantList ships_, sections_, power_;
    QVariantMap shipInfo_, totals_;
    bool modified_ = false;
    std::vector<SavedLoadout> savedLoadouts_;
    QString lastShip_;
    QString status_ = QStringLiteral("loading");
    QString builtKey_;
    bool active_ = false;
    bool building_ = false;
};
