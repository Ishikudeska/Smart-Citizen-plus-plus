#include "MissionsController.h"

#include "AppController.h"
#include "core/i18n/Translator.h"
#include "core/pipeline/Extraction.h"
#include "core/text/IniFile.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QLoggingCategory>

#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(lcApp)

using namespace core;
using namespace core::missions;

namespace {

QString text(const char *key, const QVariantHash &args = {})
{
    return core::i18n::tr(key, args);
}

// "PickupLocation" -> "Pickup Location", "Destination1" -> "Destination 1".
QString slotLabel(const QString &name)
{
    QString out;
    for (qsizetype i = 0; i < name.size(); ++i) {
        const QChar c = name[i];
        if (c == u'_') {
            out += u' ';
            continue;
        }
        if (i > 0) {
            const QChar prev = name[i - 1];
            if ((c.isUpper() && prev.isLower()) || (c.isDigit() && !prev.isDigit()))
                out += u' ';
        }
        out += c;
    }
    return out.simplified();
}

// What the game shows for a record's currencyType.
QString currencyName(const QString &currency)
{
    return currency.isEmpty() || currency == u"UEC" ? QStringLiteral("aUEC") : currency;
}

// Places listed per location in the details; the rest are counted.
constexpr int kPlacesShown = 300;

} // namespace

MissionsController::MissionsController(QObject *parent) : QObject(parent)
{
    connect(&app(), &AppController::pathsChanged, this, &MissionsController::sourcesChanged);
    connect(&app(), &AppController::languageChanged, this, &MissionsController::sourcesChanged);
    connect(&app(), &AppController::channelChanged, this, &MissionsController::sourcesChanged);
}

AppController &MissionsController::app() const
{
    return *AppController::instance();
}

void MissionsController::setActive(bool active)
{
    if (active_ == active)
        return;
    active_ = active;
    emit activeChanged();
    sourcesChanged();
}

QString MissionsController::sourceKey() const
{
    const QString cache = app().dataForgeDir();
    const QString baseIni = QFileInfo::exists(app().baseIniPath())
                                ? app().baseIniPath()
                                : app().paths().baseIni(core::kDefaultLanguage);
    QStringList parts{cache, baseIni};
    for (const QString &file : {QDir(cache).filePath(kP4kStampName), dataForgeTagTablePath(cache), baseIni}) {
        const QFileInfo info(file);
        parts << QString::number(info.size()) << QString::number(info.lastModified().toMSecsSinceEpoch());
    }
    return parts.join(u'|');
}

void MissionsController::sourcesChanged()
{
    if (active_ && !building_ && sourceKey() != builtKey_)
        build();
}

void MissionsController::reload()
{
    builtKey_.clear();
    if (!building_)
        build();
}

void MissionsController::extractGameData()
{
    app().extractDataForge(false);
}

void MissionsController::build()
{
    const QString cache = app().dataForgeDir();
    const QString records = dataForgeRecordsDir(cache);
    builtKey_ = sourceKey();
    if (!QFileInfo(records).isDir()) {
        catalog_.reset();
        refilter();
        emit catalogChanged();
        setStatus(QStringLiteral("nodata"));
        return;
    }
    building_ = true;
    setStatus(QStringLiteral("loading"));
    const QString tags = dataForgeTagTablePath(cache);
    const QString baseIni = QFileInfo::exists(app().baseIniPath())
                                ? app().baseIniPath()
                                : app().paths().baseIni(core::kDefaultLanguage);
    app().tasks()->run<std::shared_ptr<const Catalog>>(
        text("missions.loading"), true,
        [records, tags, baseIni](TaskRunner::Job &job) -> std::shared_ptr<const Catalog> {
            job.report(text("missions.loading"));
            const IniMap loc = loadIni(baseIni);
            auto catalog =
                std::make_shared<const Catalog>(buildCatalog({records, tags, &loc}, job.cancelFlag()));
            return job.cancelled() ? nullptr : catalog;
        },
        [this](std::shared_ptr<const Catalog> catalog) {
            building_ = false;
            if (!catalog) { // cancelled, or the build threw
                builtKey_.clear();
                setStatus(catalog_ ? QStringLiteral("ready") : QStringLiteral("idle"));
                return;
            }
            catalog_ = std::move(catalog);
            qCInfo(lcApp) << "missions:" << catalog_->missions.size() << "missions,"
                          << catalog_->places.size() << "places";
            refilter();
            emit catalogChanged();
            setStatus(QStringLiteral("ready"));
            sourcesChanged(); // changed while building
        });
}

void MissionsController::setStatus(const QString &status)
{
    if (status_ == status)
        return;
    status_ = status;
    emit statusChanged();
}

void MissionsController::refilter()
{
    rows_.clear();
    if (catalog_) {
        const std::vector<int> shown = filterMissions(*catalog_, filter_);
        rows_.reserve(static_cast<qsizetype>(shown.size()));
        for (const int i : shown) {
            const Mission &m = catalog_->missions[std::size_t(i)];
            rows_ << QVariantMap{{QStringLiteral("mission"), i},
                                 {QStringLiteral("title"), m.title},
                                 {QStringLiteral("giver"), m.giver},
                                 {QStringLiteral("category"), m.category},
                                 {QStringLiteral("systems"), m.systems.join(QStringLiteral(", "))},
                                 {QStringLiteral("payout"), payoutText(m.payout)},
                                 {QStringLiteral("blueprints"), !m.blueprints.empty()}};
        }
    }
    emit rowsChanged();
}

void MissionsController::setSearch(const QString &v)
{
    if (filter_.search == v)
        return;
    filter_.search = v;
    emit filtersChanged();
    refilter();
}

void MissionsController::setCategory(const QString &v)
{
    if (filter_.category == v)
        return;
    filter_.category = v;
    emit filtersChanged();
    refilter();
}

void MissionsController::setSystem(const QString &v)
{
    if (filter_.system == v)
        return;
    filter_.system = v;
    emit filtersChanged();
    refilter();
}

QString MissionsController::payout() const
{
    switch (filter_.payout) {
    case Filter::PayoutKind::Fixed:
        return QStringLiteral("fixed");
    case Filter::PayoutKind::Calculated:
        return QStringLiteral("calculated");
    case Filter::PayoutKind::Any:
        break;
    }
    return {};
}

void MissionsController::setPayout(const QString &v)
{
    const Filter::PayoutKind kind = v == u"fixed"        ? Filter::PayoutKind::Fixed
                                    : v == u"calculated" ? Filter::PayoutKind::Calculated
                                                         : Filter::PayoutKind::Any;
    if (filter_.payout == kind)
        return;
    filter_.payout = kind;
    emit filtersChanged();
    refilter();
}

QString MissionsController::sort() const
{
    switch (filter_.sort) {
    case Filter::Sort::PayoutHigh:
        return QStringLiteral("payout_high");
    case Filter::Sort::PayoutLow:
        return QStringLiteral("payout_low");
    case Filter::Sort::Title:
        break;
    }
    return QStringLiteral("title");
}

void MissionsController::setSort(const QString &v)
{
    const Filter::Sort sort = v == u"payout_high"  ? Filter::Sort::PayoutHigh
                              : v == u"payout_low" ? Filter::Sort::PayoutLow
                                                   : Filter::Sort::Title;
    if (filter_.sort == sort)
        return;
    filter_.sort = sort;
    emit filtersChanged();
    refilter();
}

void MissionsController::setBlueprintsOnly(bool v)
{
    if (filter_.blueprintsOnly == v)
        return;
    filter_.blueprintsOnly = v;
    emit filtersChanged();
    refilter();
}

QString MissionsController::payoutText(const Payout &payout) const
{
    const QLocale locale;
    switch (payout.kind) {
    case Payout::Kind::Fixed: {
        const QString currency = currencyName(payout.currency);
        if (payout.amount <= 0)
            return text("missions.payout_up_to", {{QStringLiteral("max"), locale.toString(payout.max)},
                                                  {QStringLiteral("currency"), currency}});
        if (payout.max > payout.amount)
            return text("missions.payout_range", {{QStringLiteral("min"), locale.toString(payout.amount)},
                                                  {QStringLiteral("max"), locale.toString(payout.max)},
                                                  {QStringLiteral("currency"), currency}});
        return text("missions.payout_amount", {{QStringLiteral("amount"), locale.toString(payout.amount)},
                                               {QStringLiteral("currency"), currency}});
    }
    case Payout::Kind::Calculated:
        return text("missions.payout_calculated");
    case Payout::Kind::None:
        break;
    }
    return text("missions.payout_none");
}

QVariantMap MissionsController::details(int mission) const
{
    if (!catalog_ || mission < 0 || mission >= static_cast<int>(catalog_->missions.size()))
        return {};
    const Mission &m = catalog_->missions[std::size_t(mission)];
    QVariantList locations;
    for (const LocationSlot &slot : m.locations) {
        const std::vector<int> &set = catalog_->placeSets[std::size_t(slot.placeSet)];
        QVariantList places;
        for (std::size_t i = 0; i < set.size() && i < std::size_t(kPlacesShown); ++i) {
            const Place &p = catalog_->places[std::size_t(set[i])];
            // The address usually says more ("Gundo" -> "Gundo around Daymar").
            const QString label = p.address.isEmpty() || p.address == p.name ? p.name
                                  : p.address.contains(p.name, Qt::CaseInsensitive)
                                      ? p.address
                                      : p.name + QStringLiteral(" — ") + p.address;
            places << QVariantMap{{QStringLiteral("label"), label}, {QStringLiteral("system"), p.system}};
        }
        locations << QVariantMap{
            {QStringLiteral("label"), slotLabel(slot.token.isEmpty() ? slot.variable : slot.token)},
            {QStringLiteral("count"), static_cast<int>(set.size())},
            {QStringLiteral("places"), places},
            {QStringLiteral("more"), static_cast<int>(set.size()) - static_cast<int>(places.size())},
            {QStringLiteral("tags"), slot.searchTags.join(QStringLiteral(", "))}};
    }
    // "+1,000  Covalex · Courier", success and the rest apart.
    const QLocale locale;
    QStringList repSuccess, repFailure;
    for (const ReputationReward &r : m.reputation) {
        const QString amount = (r.amount > 0 ? QStringLiteral("+") : QString()) + locale.toString(r.amount);
        QStringList who;
        for (const QString &part : {r.faction, r.scope})
            if (!part.isEmpty())
                who << part;
        (r.success ? repSuccess : repFailure)
            << amount + QStringLiteral("  ") + who.join(QStringLiteral(" · "));
    }
    QVariantList blueprints;
    for (const BlueprintReward &b : m.blueprints) {
        const QString chance =
            b.chance >= 1.0 ? text("missions.chance_guaranteed")
                            : text("missions.chance", {{QStringLiteral("percent"), qRound(b.chance * 100)}});
        blueprints << QVariantMap{
            {QStringLiteral("heading"),
             b.label.isEmpty() ? text("missions.blueprint_pool", {{QStringLiteral("chance"), chance}})
                               : text("missions.blueprint_pool_label", {{QStringLiteral("label"), b.label},
                                                                        {QStringLiteral("chance"), chance}})},
            {QStringLiteral("items"), b.items}};
    }
    const int versions = std::max(m.titleVariants, m.descriptionVariants);
    const QString currency = currencyName(m.payout.currency);
    return {
        {QStringLiteral("requiredRank"), m.requiredRank},
        {QStringLiteral("repSuccess"), repSuccess},
        {QStringLiteral("repFailure"), repFailure},
        {QStringLiteral("blueprints"), blueprints},
        {QStringLiteral("title"), m.title},
        {QStringLiteral("giver"), m.giver},
        {QStringLiteral("category"), m.category},
        {QStringLiteral("systems"), m.systems.join(QStringLiteral(", "))},
        {QStringLiteral("payout"), payoutText(m.payout)},
        {QStringLiteral("payoutNote"),
         m.payout.kind == Payout::Kind::Calculated ? text("missions.payout_calculated_note") : QString()},
        {QStringLiteral("buyIn"),
         m.payout.buyIn > 0
             ? text("missions.payout_amount", {{QStringLiteral("amount"), QLocale().toString(m.payout.buyIn)},
                                               {QStringLiteral("currency"), currency}})
             : QString()},
        {QStringLiteral("difficulty"), m.difficulty},
        {QStringLiteral("description"), m.description},
        {QStringLiteral("variants"),
         versions > 1 ? text("missions.variants_note", {{QStringLiteral("count"), versions}}) : QString()},
        {QStringLiteral("source"), text(m.contract ? "missions.source_contract" : "missions.source_broker")},
        {QStringLiteral("record"), m.file},
        {QStringLiteral("locations"), locations},
    };
}
