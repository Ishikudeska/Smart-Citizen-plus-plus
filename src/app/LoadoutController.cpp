#include "LoadoutController.h"

#include "AppController.h"
#include "core/i18n/Translator.h"
#include "core/pipeline/Extraction.h"
#include "core/text/IniFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

Q_DECLARE_LOGGING_CATEGORY(lcApp)

using namespace core;
using namespace core::loadout;

namespace {

QString text(const char *key, const QVariantHash &args = {})
{
    return core::i18n::tr(key, args);
}

// 1234.5 -> "1,235"; with decimals, trailing zeros go: "4.6", "11".
QString num(double v, int decimals = 0)
{
    if (!std::isfinite(v))
        return QStringLiteral("–");
    QString s = QLocale().toString(v, 'f', decimals);
    if (decimals > 0) {
        const QChar point = QLocale().decimalPoint().front();
        while (s.endsWith(u'0'))
            s.chop(1);
        if (s.endsWith(point))
            s.chop(1);
    }
    return s;
}

QString metres(double m)
{
    return m >= 10'000 ? num(m / 1000, 1) + QStringLiteral(" km") : num(m) + QStringLiteral(" m");
}

QString seconds(double s)
{
    if (s <= 0 || !std::isfinite(s))
        return QStringLiteral("–");
    if (s < 60)
        return num(s, 1) + QStringLiteral(" s");
    const qint64 whole = qRound64(s);
    if (whole < 3600)
        return QStringLiteral("%1 min %2 s").arg(whole / 60).arg(whole % 60);
    return QStringLiteral("%1 h %2 min").arg(whole / 3600).arg(whole % 3600 / 60);
}

QString percent(double fraction)
{
    return num(fraction * 100) + u'%';
}

// 1.1 -> "+10%", 0.9 -> "-10%".
QString signedPercent(double multiplier)
{
    const double p = (multiplier - 1) * 100;
    return (p > 0 ? QStringLiteral("+") : QString()) + num(p) + u'%';
}

QString sizeText(const Port &p)
{
    if (p.maxSize <= 0)
        return QString();
    return p.minSize == p.maxSize || p.minSize <= 0 ? QStringLiteral("S%1").arg(p.maxSize)
                                                    : QStringLiteral("S%1–%2").arg(p.minSize).arg(p.maxSize);
}

// "hardpoint_weapon_class2_nose" -> "Weapon Class2 Nose".
QString portLabel(const Port &p)
{
    if (!p.label.isEmpty())
        return p.label;
    QString s = p.name;
    static const QRegularExpression prefix(QStringLiteral("^hardpoint_"),
                                           QRegularExpression::CaseInsensitiveOption);
    s.remove(prefix);
    QStringList words = s.split(u'_', Qt::SkipEmptyParts);
    for (QString &w : words)
        if (!w.isEmpty())
            w[0] = w[0].toUpper();
    return words.join(u' ');
}

QString trackingShort(const QString &signal)
{
    if (signal == u"Infrared")
        return QStringLiteral("IR");
    if (signal == u"Electromagnetic")
        return QStringLiteral("EM");
    if (signal == u"CrossSection")
        return QStringLiteral("CS");
    return signal;
}

QVariantMap statRow(const char *labelKey, const QString &value)
{
    return {{QStringLiteral("label"), text(labelKey)}, {QStringLiteral("value"), value}};
}

// The page section an item type shows in; "" for the ones it doesn't.
QString sectionOfType(const QString &t)
{
    if (t == u"WeaponGun" || t == u"Turret" || t == u"TurretBase" || t == u"WeaponMount")
        return QStringLiteral("armament");
    if (t == u"MissileLauncher" || t == u"BombLauncher" || t == u"Missile" || t == u"Bomb")
        return QStringLiteral("ordnance");
    if (t == u"Shield")
        return QStringLiteral("shields");
    if (t == u"PowerPlant")
        return QStringLiteral("power");
    if (t == u"Cooler")
        return QStringLiteral("coolers");
    if (t == u"QuantumDrive")
        return QStringLiteral("quantum");
    if (t == u"Radar")
        return QStringLiteral("radar");
    if (t == u"LifeSupportGenerator")
        return QStringLiteral("lifesupport");
    if (t == u"WeaponMining" || t == u"UtilityTurret" || t == u"TractorBeam" || t == u"TowingBeam" ||
        t == u"SalvageHead" || t == u"EMP" || t == u"QuantumInterdictionGenerator" || t == u"ToolArm" ||
        t == u"SalvageModifier" || t == u"MiningModifier")
        return QStringLiteral("utility");
    if (t == u"WeaponDefensive")
        return QStringLiteral("countermeasures");
    if (t == u"Paints")
        return QStringLiteral("paint");
    if (t == u"Module")
        return QStringLiteral("modules");
    return QString();
}

QString sectionOf(const Slot &s)
{
    if (!s.item)
        return s.port.types.empty() ? QString() : sectionOfType(s.port.types.front().type);
    const QString own = sectionOfType(s.item->type);
    // A turret is armament when it holds guns, utility when it holds tools.
    if (own == u"armament" && s.item->type != u"WeaponGun") {
        QString held;
        const auto look = [&](const auto &self, const std::vector<Slot> &list) -> void {
            for (const Slot &c : list) {
                if (!held.isEmpty())
                    return;
                if (c.item) {
                    const QString sec = sectionOfType(c.item->type);
                    if (sec == u"utility" || (sec == u"armament" && c.item->type == u"WeaponGun"))
                        held = sec;
                }
                self(self, c.children);
            }
        };
        look(look, s.children);
        if (held == u"utility")
            return held;
    }
    return own;
}

constexpr const char *kSectionOrder[] = {"armament", "ordnance",        "utility", "shields",
                                         "power",    "coolers",         "quantum", "lifesupport",
                                         "radar",    "countermeasures", "modules", "paint"};

QString categoryLabel(const QString &category)
{
    const QByteArray key = "loadout.power_" + category.toUtf8();
    return text(key.constData());
}

} // namespace

LoadoutController::LoadoutController(QObject *parent) : QObject(parent)
{
    connect(&app(), &AppController::pathsChanged, this, &LoadoutController::sourcesChanged);
    connect(&app(), &AppController::pathsChanged, this, [this] {
        loadSaved(); // the user data folder may have moved
        emit savedChanged();
    });
    connect(&app(), &AppController::languageChanged, this, &LoadoutController::sourcesChanged);
    connect(&app(), &AppController::channelChanged, this, &LoadoutController::sourcesChanged);
    loadSaved();
}

AppController &LoadoutController::app() const
{
    return *AppController::instance();
}

void LoadoutController::setActive(bool active)
{
    if (active_ == active)
        return;
    active_ = active;
    emit activeChanged();
    sourcesChanged();
}

QString LoadoutController::sourceKey() const
{
    const QString cache = app().dataForgeDir();
    const QString baseIni = QFileInfo::exists(app().baseIniPath())
                                ? app().baseIniPath()
                                : app().paths().baseIni(core::kDefaultLanguage);
    QStringList parts{cache, baseIni};
    for (const QString &file : {QDir(cache).filePath(kP4kStampName), dataForgeVehiclesDir(cache), baseIni}) {
        const QFileInfo info(file);
        parts << QString::number(info.size()) << QString::number(info.lastModified().toMSecsSinceEpoch());
    }
    return parts.join(u'|');
}

void LoadoutController::sourcesChanged()
{
    if (active_ && !building_ && sourceKey() != builtKey_)
        build();
}

void LoadoutController::reload()
{
    builtKey_.clear();
    if (!building_)
        build();
}

void LoadoutController::extractGameData()
{
    app().extractDataForge(false);
}

void LoadoutController::setStatus(const QString &status)
{
    if (status_ == status)
        return;
    status_ = status;
    emit statusChanged();
}

void LoadoutController::build()
{
    const QString cache = app().dataForgeDir();
    const QString records = dataForgeRecordsDir(cache);
    builtKey_ = sourceKey();
    if (!QFileInfo(records).isDir()) {
        catalog_.reset();
        ships_.clear();
        selectShip(nullptr);
        emit catalogChanged();
        setStatus(QStringLiteral("nodata"));
        return;
    }
    building_ = true;
    setStatus(QStringLiteral("loading"));
    const QString vehicles = dataForgeVehiclesDir(cache);
    const QString baseIni = QFileInfo::exists(app().baseIniPath())
                                ? app().baseIniPath()
                                : app().paths().baseIni(core::kDefaultLanguage);
    app().tasks()->run<std::shared_ptr<const Catalog>>(
        text("loadout.loading"), true,
        [records, vehicles, baseIni](TaskRunner::Job &job) -> std::shared_ptr<const Catalog> {
            job.report(text("loadout.loading"));
            const IniMap loc = loadIni(baseIni);
            auto catalog =
                std::make_shared<const Catalog>(buildCatalog({records, vehicles, &loc}, job.cancelFlag()));
            return job.cancelled() ? nullptr : catalog;
        },
        [this](std::shared_ptr<const Catalog> catalog) {
            building_ = false;
            if (!catalog) { // cancelled, or the build threw
                builtKey_.clear();
                setStatus(catalog_ ? QStringLiteral("ready") : QStringLiteral("idle"));
                return;
            }
            const QString keep = loadout_.ship() ? loadout_.ship()->id : lastShip_;
            loadout_ = Loadout();
            catalog_ = std::move(catalog);
            qCInfo(lcApp) << "loadouts:" << catalog_->ships.size() << "ships," << catalog_->items.size()
                          << "items";
            ships_.clear();
            for (const Ship &s : catalog_->ships)
                ships_ << QVariantMap{{QStringLiteral("id"), s.id},
                                      {QStringLiteral("name"), s.name},
                                      {QStringLiteral("manufacturer"), s.manufacturer},
                                      {QStringLiteral("size"), s.size},
                                      {QStringLiteral("career"), s.career},
                                      {QStringLiteral("role"), s.role},
                                      {QStringLiteral("ground"), s.groundVehicle}};
            emit catalogChanged();
            const Ship *pick = catalog_->ship(keep);
            if (!pick && !catalog_->ships.empty())
                pick = &catalog_->ships.front();
            selectShip(pick);
            setStatus(QStringLiteral("ready"));
            emit savedChanged();
            sourcesChanged(); // changed while building
        });
}

QString LoadoutController::shipId() const
{
    return loadout_.ship() ? loadout_.ship()->id : QString();
}

void LoadoutController::setShipId(const QString &id)
{
    if (!catalog_ || id == shipId())
        return;
    if (const Ship *s = catalog_->ship(id)) {
        selectShip(s);
        emit savedChanged();
        lastShip_ = s->id;
        writeSaved();
    }
}

void LoadoutController::selectShip(const Ship *ship)
{
    plan_ = PowerPlan();
    loadout_ = ship && catalog_ ? Loadout(*catalog_, *ship) : Loadout();
    refresh();
}

void LoadoutController::setNav(bool nav)
{
    if (plan_.nav == nav)
        return;
    plan_.nav = nav;
    plan_.pips.clear(); // the modes split power differently
    refresh();
}

void LoadoutController::setItem(const QString &path, const QString &itemId)
{
    if (loadout_.setItem(path, itemId))
        refresh();
}

void LoadoutController::resetLoadout()
{
    if (loadout_.ship())
        selectShip(loadout_.ship());
}

void LoadoutController::setPips(const QString &key, int pips)
{
    plan_.pips.insert(key, pips);
    refresh();
}

void LoadoutController::resetPower()
{
    plan_.pips.clear();
    refresh();
}

void LoadoutController::refresh()
{
    const Ship *ship = loadout_.ship();
    shipInfo_.clear();
    sections_.clear();
    totals_.clear();
    power_.clear();
    modified_ = false;
    if (!ship) {
        emit loadoutChanged();
        return;
    }
    // Pips for consumers the loadout no longer has go.
    const std::vector<Consumer> consumers = core::loadout::consumers(loadout_, plan_);
    for (auto it = plan_.pips.begin(); it != plan_.pips.end();) {
        const bool present = std::any_of(consumers.begin(), consumers.end(),
                                         [&](const Consumer &c) { return c.key == it.key(); });
        it = present ? std::next(it) : plan_.pips.erase(it);
    }
    current_ = computeTotals(loadout_, consumers);
    const Totals &t = current_;
    modified_ = !loadout_.changes().isEmpty();

    int vitalParts = 0;
    for (const HullPart &p : ship->hull)
        vitalParts += p.vital;
    shipInfo_ = {
        {QStringLiteral("id"), ship->id},
        {QStringLiteral("name"), ship->name},
        {QStringLiteral("manufacturer"), ship->manufacturer},
        {QStringLiteral("description"), ship->description},
        {QStringLiteral("career"), ship->career},
        {QStringLiteral("role"), ship->role},
        {QStringLiteral("size"), ship->size > 0 ? QStringLiteral("S%1").arg(ship->size) : QString()},
        {QStringLiteral("crew"),
         ship->crew > 0 ? text("loadout.crew", {{QStringLiteral("n"), ship->crew}}) : QString()},
        {QStringLiteral("ground"), ship->groundVehicle},
        {QStringLiteral("dimensions"),
         ship->length > 0 ? QStringLiteral("%1 × %2 × %3 m")
                                .arg(num(ship->length, 1), num(ship->width, 1), num(ship->height, 1))
                          : QString()},
        {QStringLiteral("mass"), num(ship->mass) + QStringLiteral(" kg")},
        {QStringLiteral("loadedMass"), num(t.loadedMass) + QStringLiteral(" kg")},
        {QStringLiteral("hullHp"), num(t.hullHp)},
        {QStringLiteral("vitalHp"), num(t.vitalHp)},
        {QStringLiteral("vitalParts"), vitalParts},
        {QStringLiteral("otherParts"), int(ship->hull.size()) - vitalParts},
        {QStringLiteral("otherHp"), num(t.hullHp - t.vitalHp)},
    };

    // Sections of cards, in page order.
    QHash<QString, QVariantList> cards;
    for (const Slot &s : loadout_.roots()) {
        const QString section = sectionOf(s);
        if (section.isEmpty() || (!s.item && (!s.port.editable || s.port.hidden)))
            continue;
        cards[section] << card(s);
    }
    for (const char *key : kSectionOrder) {
        const QString k = QLatin1StringView(key);
        if (!cards.contains(k))
            continue;
        const QByteArray titleKey = "loadout.section_" + QByteArray(key);
        sections_ << QVariantMap{{QStringLiteral("key"), k},
                                 {QStringLiteral("title"), text(titleKey.constData())},
                                 {QStringLiteral("cards"), cards.value(k)}};
    }

    for (const Consumer &c : consumers)
        power_ << QVariantMap{
            {QStringLiteral("key"), c.key},
            {QStringLiteral("label"), c.label.isEmpty() ? categoryLabel(c.category) : c.label},
            {QStringLiteral("category"), c.category},
            {QStringLiteral("categoryLabel"), categoryLabel(c.category)},
            {QStringLiteral("min"), c.min},
            {QStringLiteral("max"), c.max},
            {QStringLiteral("pips"), c.pips}};

    // Damage.
    QVariantList groups;
    const std::pair<const char *, const Totals::Group *> named[] = {{"loadout.group_pilot", &t.pilot},
                                                                    {"loadout.group_manned", &t.manned},
                                                                    {"loadout.group_remote", &t.remote},
                                                                    {"loadout.group_pds", &t.pds}};
    for (const auto &[key, g] : named)
        if (g->guns > 0)
            groups << QVariantMap{{QStringLiteral("label"), text(key)},
                                  {QStringLiteral("guns"), g->guns},
                                  {QStringLiteral("alpha"), num(g->alpha)},
                                  {QStringLiteral("burst"), num(g->burst)},
                                  {QStringLiteral("sustained"), num(g->sustained)}};
    const QVariantMap damage{
        {QStringLiteral("alpha"), num(t.alpha)},
        {QStringLiteral("burst"), num(t.burstDps)},
        {QStringLiteral("sustained"), num(t.sustainedDps)},
        {QStringLiteral("groups"), groups},
        {QStringLiteral("missiles"),
         t.missiles > 0 ? QStringLiteral("%1 × · %2").arg(t.missiles).arg(num(t.missileDamage)) : QString()},
        {QStringLiteral("bombs"),
         t.bombs > 0 ? QStringLiteral("%1 × · %2").arg(t.bombs).arg(num(t.bombDamage)) : QString()},
        {QStringLiteral("efficiency"), percent(t.weaponEfficiency)},
        {QStringLiteral("physical"), num(t.burstByType.physical)},
        {QStringLiteral("energy"), num(t.burstByType.energy)},
        {QStringLiteral("distortion"), num(t.burstByType.distortion)},
    };

    // Defense.
    const auto three = [](const Damage &d, auto fmt) {
        return QVariantMap{{QStringLiteral("physical"), fmt(d.physical)},
                           {QStringLiteral("energy"), fmt(d.energy)},
                           {QStringLiteral("distortion"), fmt(d.distortion)}};
    };
    const auto reduction = [](double multiplier) { return percent(1 - multiplier); };
    const QVariantMap defense{
        {QStringLiteral("shieldHp"), num(t.shieldHp)},
        {QStringLiteral("shieldRegen"), num(t.shieldRegen)},
        {QStringLiteral("generators"), t.shieldGenerators},
        {QStringLiteral("resistance"), three(t.shieldResistance, percent)},
        {QStringLiteral("absorption"), three(t.shieldAbsorption, percent)},
        {QStringLiteral("armorHp"), num(t.armorHp)},
        {QStringLiteral("deflection"), three(t.armorDeflection, [](double v) { return num(v); })},
        {QStringLiteral("reduction"), three(t.armorMultiplier, reduction)},
        {QStringLiteral("em"), signedPercent(t.emMultiplier)},
        {QStringLiteral("ir"), signedPercent(t.irMultiplier)},
        {QStringLiteral("cs"), signedPercent(t.csMultiplier)},
        {QStringLiteral("decoys"), t.decoys},
        {QStringLiteral("noise"), t.noise},
    };

    const QVariantMap powerTotals{
        {QStringLiteral("output"), num(t.powerOutput)},
        {QStringLiteral("used"), t.powerUsed},
        {QStringLiteral("free"), int(std::floor(t.powerOutput + 1e-9)) - t.powerUsed},
        {QStringLiteral("over"), t.powerUsed > int(std::floor(t.powerOutput + 1e-9))},
        {QStringLiteral("coolant"), num(t.coolantOutput, 1)},
        {QStringLiteral("em"), num(t.em)},
        {QStringLiteral("ir"), num(t.ir)},
    };

    QVariantMap flight;
    if (t.flight) {
        const FlightStats &f = *t.flight;
        flight = {
            {QStringLiteral("scm"), num(f.scmSpeed) + QStringLiteral(" m/s")},
            {QStringLiteral("boost"),
             num(f.boostForward) + QStringLiteral(" / ") + num(f.boostBackward) + QStringLiteral(" m/s")},
            {QStringLiteral("max"), num(f.maxSpeed) + QStringLiteral(" m/s")},
            {QStringLiteral("pitch"), num(f.pitch) + QStringLiteral(" / ") + num(f.pitch * f.pitchBoost)},
            {QStringLiteral("yaw"), num(f.yaw) + QStringLiteral(" / ") + num(f.yaw * f.yawBoost)},
            {QStringLiteral("roll"), num(f.roll) + QStringLiteral(" / ") + num(f.roll * f.rollBoost)},
            {QStringLiteral("boostTank"),
             f.boostCapacity > 0 && f.boostRegen > 0
                 ? text("loadout.boost_tank",
                        {{QStringLiteral("time"), seconds(f.boostCapacity / f.boostRegen)},
                         {QStringLiteral("delay"), seconds(f.boostRegenDelay)}})
                 : QString()},
        };
    }
    flight.insert(QStringLiteral("mainThrust"), num(t.mainThrust / 1e6, 2) + QStringLiteral(" MN"));
    flight.insert(QStringLiteral("retroThrust"), num(t.retroThrust / 1e6, 2) + QStringLiteral(" MN"));
    flight.insert(QStringLiteral("maneuverThrust"), num(t.maneuverThrust / 1e6, 2) + QStringLiteral(" MN"));
    flight.insert(QStringLiteral("accel"),
                  t.accelForward > 0 ? num(t.accelForward / 9.80665, 1) + QStringLiteral(" g") : QString());
    flight.insert(QStringLiteral("fuel"), num(t.hydrogenFuel, 2) + QStringLiteral(" SCU"));
    flight.insert(QStringLiteral("quantumFuel"), num(t.quantumFuel, 2) + QStringLiteral(" SCU"));

    QVariantMap quantum;
    if (t.quantum) {
        const QuantumStats &q = *t.quantum;
        quantum = {
            {QStringLiteral("speed"), num(q.speed / 1000) + QStringLiteral(" km/s")},
            {QStringLiteral("spline"), num(q.splineSpeed / 1000) + QStringLiteral(" km/s")},
            {QStringLiteral("spool"), seconds(q.spoolTime)},
            {QStringLiteral("cooldown"), seconds(q.cooldown)},
            {QStringLiteral("accel"), num(q.stageOneAccel / 1000) + QStringLiteral(" / ") +
                                          num(q.stageTwoAccel / 1000) + QStringLiteral(" km/s²")},
            {QStringLiteral("fuel"), num(q.fuelPerGm * 1000, 2) + QStringLiteral(" mSCU/Gm")},
            {QStringLiteral("range"),
             t.quantumRange > 0 ? num(t.quantumRange) + QStringLiteral(" Gm") : QString()},
        };
    }

    totals_ = {{QStringLiteral("damage"), damage},
               {QStringLiteral("defense"), defense},
               {QStringLiteral("power"), powerTotals},
               {QStringLiteral("flight"), flight},
               {QStringLiteral("quantum"), quantum}};
    emit loadoutChanged();
}

QVariantMap LoadoutController::card(const Slot &slot) const
{
    // The slot, then what its item holds (a gimbal's gun, a rack's missiles,
    // a turret's guns), flattened with their depth.
    QVariantList rows;
    const auto add = [&](const auto &self, const Slot &s, int depth) -> void {
        rows << QVariantMap{{QStringLiteral("path"), s.path},
                            {QStringLiteral("label"), portLabel(s.port)},
                            {QStringLiteral("size"), sizeText(s.port)},
                            {QStringLiteral("editable"), s.port.editable},
                            {QStringLiteral("depth"), depth},
                            {QStringLiteral("item"), s.item ? QVariant(itemMap(*s.item)) : QVariant()}};
        for (const Slot &c : s.children)
            if (!sectionOf(c).isEmpty() && (c.item || (c.port.editable && !c.port.hidden)))
                self(self, c, depth + 1);
    };
    add(add, slot, 0);
    return {{QStringLiteral("path"), slot.path}, {QStringLiteral("rows"), rows}};
}

QVariantMap LoadoutController::itemMap(const Item &item) const
{
    return {{QStringLiteral("id"), item.id},
            {QStringLiteral("name"), item.name},
            {QStringLiteral("maker"),
             item.manufacturerCode.isEmpty() ? item.manufacturer : item.manufacturerCode},
            {QStringLiteral("size"), item.size > 0 ? QStringLiteral("S%1").arg(item.size) : QString()},
            {QStringLiteral("grade"), item.gradeLetter()},
            {QStringLiteral("cls"), item.itemClass},
            {QStringLiteral("stats"), statsOf(item)}};
}

QVariantList LoadoutController::statsOf(const Item &item) const
{
    QVariantList out;
    if (const auto &w = item.weapon) {
        out << statRow("loadout.stat_alpha", num(w->alpha(), 1))
            << statRow("loadout.stat_rate", num(w->fireRate) + QStringLiteral(" rpm"))
            << statRow("loadout.stat_burst", num(w->burstDps()))
            << statRow("loadout.stat_sustained", num(w->sustainedDps(current_.weaponEfficiency)));
        if (w->regenerates)
            out << statRow("loadout.stat_capacitor", num(w->ammo));
        else if (w->ammo > 0)
            out << statRow("loadout.stat_ammo", num(w->ammo));
        out << statRow("loadout.stat_speed", num(w->speed) + QStringLiteral(" m/s"))
            << statRow("loadout.stat_range", metres(w->range));
        if (w->penetration > 0)
            out << statRow("loadout.stat_penetration", num(w->penetration, 2) + QStringLiteral(" m"));
        if (const double fire = w->timeToOverheat(); fire > 0)
            out << statRow("loadout.stat_overheat",
                           seconds(fire) + QStringLiteral(" / ") + seconds(w->overheatFixTime));
    }
    if (const auto &m = item.missile)
        out << statRow("loadout.stat_damage", num(m->damage.total()))
            << statRow("loadout.stat_tracking", trackingShort(m->tracking))
            << statRow("loadout.stat_lock", seconds(m->lockTime))
            << statRow("loadout.stat_lock_range",
                       metres(m->lockRangeMin) + QStringLiteral(" – ") + metres(m->lockRangeMax))
            << statRow("loadout.stat_speed", num(m->speed) + QStringLiteral(" m/s"));
    if (const auto &s = item.shield)
        out << statRow("loadout.stat_shield_hp", num(s->hp))
            << statRow("loadout.stat_regen", num(s->regen) + QStringLiteral(" HP/s"))
            << statRow("loadout.stat_delays",
                       seconds(s->damagedDelay) + QStringLiteral(" / ") + seconds(s->downedDelay));
    if (item.resources.powerOutput > 0)
        out << statRow("loadout.stat_power_output", num(item.resources.powerOutput));
    if (item.resources.coolantOutput > 0)
        out << statRow("loadout.stat_coolant", num(item.resources.coolantOutput));
    if (const auto &q = item.quantum)
        out << statRow("loadout.stat_speed", num(q->speed / 1000) + QStringLiteral(" km/s"))
            << statRow("loadout.stat_spool", seconds(q->spoolTime))
            << statRow("loadout.stat_cooldown", seconds(q->cooldown))
            << statRow("loadout.stat_qfuel", num(q->fuelPerGm * 1000, 2) + QStringLiteral(" mSCU/Gm"));
    if (const auto &c = item.countermeasure)
        out << statRow(c->kind == u"Noise" ? "loadout.stat_noise" : "loadout.stat_decoys", num(c->ammo));
    if (item.resources.powerDraw > 0 && item.type != u"WeaponGun")
        out << statRow("loadout.stat_power", num(item.resources.powerDraw, 1));
    if (item.resources.em > 0)
        out << statRow("loadout.stat_em", num(item.resources.em));
    if (item.resources.ir > 0)
        out << statRow("loadout.stat_ir", num(item.resources.ir));
    return out;
}

QVariantList LoadoutController::compatibleItems(const QString &path) const
{
    QVariantList out;
    const Slot *slot = loadout_.find(path);
    if (!slot)
        return out;
    for (const Item *item : loadout_.compatible(path)) {
        QStringList summary;
        const QVariantList stats = statsOf(*item);
        for (qsizetype i = 0; i < stats.size() && i < 4; ++i) {
            const QVariantMap m = stats[i].toMap();
            summary << m.value(QStringLiteral("label")).toString() + u' ' +
                           m.value(QStringLiteral("value")).toString();
        }
        out << QVariantMap{{QStringLiteral("id"), item->id},
                           {QStringLiteral("name"), item->name},
                           {QStringLiteral("maker"),
                            item->manufacturerCode.isEmpty() ? item->manufacturer : item->manufacturerCode},
                           {QStringLiteral("size"), QStringLiteral("S%1").arg(item->size)},
                           {QStringLiteral("grade"), item->gradeLetter()},
                           {QStringLiteral("cls"), item->itemClass},
                           {QStringLiteral("summary"), summary.join(QStringLiteral("  ·  "))},
                           {QStringLiteral("current"), item == slot->item}};
    }
    return out;
}

QString LoadoutController::travelTime(double gigametres) const
{
    if (!current_.quantum || gigametres <= 0)
        return QStringLiteral("–");
    return seconds(current_.quantum->spoolTime + quantumTravelTime(*current_.quantum, gigametres * 1e9));
}

QVariantMap LoadoutController::engage(const QString &targetShipId) const
{
    if (!catalog_)
        return {};
    const Ship *target = catalog_->ship(targetShipId);
    if (!target)
        return {};
    const Loadout stock(*catalog_, *target);
    const Totals targetTotals = computeTotals(stock, consumers(stock, {}));
    const Engagement e = core::loadout::engage(current_, targetTotals);
    return {{QStringLiteral("target"), target->name},
            {QStringLiteral("shieldHp"), num(targetTotals.shieldHp)},
            {QStringLiteral("hullHp"),
             num(targetTotals.vitalHp > 0 ? targetTotals.vitalHp : targetTotals.hullHp)},
            {QStringLiteral("shieldTime"),
             targetTotals.shieldHp > 0 ? seconds(e.shieldTime) : QStringLiteral("–")},
            {QStringLiteral("killTime"), seconds(e.killTime)},
            {QStringLiteral("deflected"), e.deflected}};
}

// ── saved loadouts ──────────────────────────────────────────────────────────

QString LoadoutController::savedFile() const
{
    return QDir(app().userDataRoot()).filePath(QStringLiteral("loadouts.json"));
}

void LoadoutController::loadSaved()
{
    savedLoadouts_.clear();
    QFile f(savedFile());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    lastShip_ = root.value(QStringLiteral("lastShip")).toString();
    const QJsonArray loadouts = root.value(QStringLiteral("loadouts")).toArray();
    for (const QJsonValue &v : loadouts) {
        const QJsonObject o = v.toObject();
        SavedLoadout s;
        s.name = o.value(QStringLiteral("name")).toString();
        s.ship = o.value(QStringLiteral("ship")).toString();
        s.nav = o.value(QStringLiteral("nav")).toBool();
        const QJsonArray items = o.value(QStringLiteral("items")).toArray();
        for (const QJsonValue &e : items) {
            const QJsonArray pair = e.toArray();
            if (pair.size() == 2)
                s.items.append({pair[0].toString(), pair[1].toString()});
        }
        const QJsonObject pips = o.value(QStringLiteral("pips")).toObject();
        for (auto it = pips.begin(); it != pips.end(); ++it)
            s.pips.insert(it.key(), it.value().toInt());
        if (!s.name.isEmpty() && !s.ship.isEmpty())
            savedLoadouts_.push_back(std::move(s));
    }
}

void LoadoutController::writeSaved() const
{
    QJsonArray list;
    for (const SavedLoadout &s : savedLoadouts_) {
        QJsonArray items;
        for (const auto &[path, id] : s.items)
            items.append(QJsonArray{path, id});
        QJsonObject pips;
        for (auto it = s.pips.begin(); it != s.pips.end(); ++it)
            pips.insert(it.key(), it.value());
        list.append(QJsonObject{{QStringLiteral("name"), s.name},
                                {QStringLiteral("ship"), s.ship},
                                {QStringLiteral("nav"), s.nav},
                                {QStringLiteral("items"), items},
                                {QStringLiteral("pips"), pips}});
    }
    const QJsonObject root{{QStringLiteral("version"), 1},
                           {QStringLiteral("lastShip"), lastShip_},
                           {QStringLiteral("loadouts"), list}};
    QDir().mkpath(QFileInfo(savedFile()).absolutePath());
    QSaveFile f(savedFile());
    if (!f.open(QIODevice::WriteOnly)) {
        qCWarning(lcApp) << "cannot write" << savedFile();
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit())
        qCWarning(lcApp) << "cannot write" << savedFile();
}

QStringList LoadoutController::saved() const
{
    QStringList names;
    const QString ship = shipId();
    for (const SavedLoadout &s : savedLoadouts_)
        if (s.ship.compare(ship, Qt::CaseInsensitive) == 0)
            names << s.name;
    names.sort(Qt::CaseInsensitive);
    return names;
}

void LoadoutController::saveLoadout(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || !loadout_.ship())
        return;
    SavedLoadout s{trimmed, loadout_.ship()->id, loadout_.entries(), plan_.pips, plan_.nav};
    const auto same = std::find_if(savedLoadouts_.begin(), savedLoadouts_.end(), [&](const SavedLoadout &o) {
        return o.ship == s.ship && o.name.compare(s.name, Qt::CaseInsensitive) == 0;
    });
    if (same != savedLoadouts_.end())
        *same = std::move(s);
    else
        savedLoadouts_.push_back(std::move(s));
    writeSaved();
    emit savedChanged();
}

void LoadoutController::loadLoadout(const QString &name)
{
    if (!catalog_)
        return;
    for (const SavedLoadout &s : savedLoadouts_) {
        if (s.ship != shipId() || s.name.compare(name, Qt::CaseInsensitive) != 0)
            continue;
        const Ship *ship = catalog_->ship(s.ship);
        if (!ship)
            return;
        loadout_ = Loadout(*catalog_, *ship);
        if (const int skipped = loadout_.apply(s.items); skipped > 0)
            qCInfo(lcApp) << "loadout" << s.name << ":" << skipped
                          << "items no longer fit and were left stock";
        plan_.nav = s.nav;
        plan_.pips = s.pips;
        refresh();
        return;
    }
}

void LoadoutController::deleteLoadout(const QString &name)
{
    const QString ship = shipId();
    const auto before = savedLoadouts_.size();
    std::erase_if(savedLoadouts_, [&](const SavedLoadout &s) {
        return s.ship == ship && s.name.compare(name, Qt::CaseInsensitive) == 0;
    });
    if (savedLoadouts_.size() != before) {
        writeSaved();
        emit savedChanged();
    }
}
