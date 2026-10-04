#include "BlueprintController.h"

#include "AppController.h"

#include "core/blueprints/BlueprintExport.h"
#include "core/blueprints/LogScanner.h"
#include "core/blueprints/OwnedItems.h"
#include "core/i18n/Translator.h"

#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QStandardPaths>

#include <algorithm>

Q_DECLARE_LOGGING_CATEGORY(lcApp)

using namespace core;
using namespace core::blueprints;

namespace {

QString text(const char *key, const QVariantHash &args = {})
{
    return core::i18n::tr(key, args);
}

void sortCaseless(QStringList &list)
{
    std::sort(list.begin(), list.end(), [](const QString &a, const QString &b) {
        const QString la = a.toLower(), lb = b.toLower();
        return la != lb ? la < lb : a < b;
    });
}

QStringList sortedFacet(QSet<QString> values, const QString &attr)
{
    QStringList out(values.begin(), values.end());
    std::sort(out.begin(), out.end(), [&attr](const QString &a, const QString &b) {
        if (attr == u"size") {
            bool okA = false, okB = false;
            const int ia = a.toInt(&okA), ib = b.toInt(&okB);
            if (okA && okB)
                return ia < ib;
        }
        const bool otherA = a == u"Other", otherB = b == u"Other";
        if (otherA != otherB)
            return otherB;
        return a < b;
    });
    return out;
}

} // namespace

BlueprintController::BlueprintController(QObject *parent) : QObject(parent)
{
    connect(&app(), &AppController::blueprintsChanged, this, &BlueprintController::rebuild);
    rebuild();
}

AppController &BlueprintController::app() const
{
    return *AppController::instance();
}

int BlueprintController::totalCount() const
{
    return static_cast<int>(app().blueprintMeta().size() + app().settings().ownedItems().size());
}

bool BlueprintController::visible(const QString &name) const
{
    if (!search_.trimmed().isEmpty() && !name.contains(search_.trimmed(), Qt::CaseInsensitive))
        return false;
    const auto &meta = app().blueprintMeta();
    const auto it = meta.constFind(name);
    if (!mission_.isEmpty() && (it == meta.cend() || !it->missions.contains(mission_)))
        return false;
    const auto facet = [&](const QString &selected, auto member) {
        if (selected.isEmpty())
            return true;
        return it != meta.cend() && (*it).*member == selected;
    };
    return facet(type_, &BlueprintItem::type) && facet(cls_, &BlueprintItem::cls) &&
           facet(size_, &BlueprintItem::size) && facet(grade_, &BlueprintItem::grade);
}

QVariantMap BlueprintController::row(const QString &name) const
{
    const auto &meta = app().blueprintMeta();
    const auto it = meta.constFind(name);
    QString display = name;
    QString tip;
    if (it != meta.cend()) {
        if (showTags() && !it->taggedName.isEmpty())
            display = it->taggedName;
        QStringList bits;
        QStringList attrs;
        for (const QString &p : {it->type, it->cls, it->size, it->grade})
            if (!p.isEmpty())
                attrs << p;
        if (!attrs.isEmpty())
            bits << attrs.join(u' ');
        if (!it->missions.isEmpty()) {
            QStringList missions(it->missions.begin(), it->missions.end());
            missions.sort();
            QString listed;
            for (const QString &m : missions)
                listed += QStringLiteral("\n  • ") + m;
            bits << text("enhancements.blueprints_tooltip_missions", {{QStringLiteral("missions"), QString()}}).trimmed() + listed;
        }
        tip = bits.join(u'\n');
    }
    return {{QStringLiteral("name"), name}, {QStringLiteral("display"), display}, {QStringLiteral("tooltip"), tip}};
}

void BlueprintController::rebuild()
{
    const auto &meta = app().blueprintMeta();
    const QSet<QString> owned = app().settings().ownedItems();
    QSet<QString> missions, types, classes, sizes, grades;
    for (auto it = meta.cbegin(); it != meta.cend(); ++it) {
        missions.unite(it->missions);
        for (const auto &[set, value] : {std::pair{&types, it->type}, std::pair{&classes, it->cls},
                                         std::pair{&sizes, it->size}, std::pair{&grades, it->grade}})
            if (!value.isEmpty())
                set->insert(value);
    }
    missions_ = QStringList(missions.begin(), missions.end());
    sortCaseless(missions_);
    types_ = sortedFacet(types, QStringLiteral("type"));
    classes_ = sortedFacet(classes, QStringLiteral("cls"));
    sizes_ = sortedFacet(sizes, QStringLiteral("size"));
    grades_ = sortedFacet(grades, QStringLiteral("grade"));
    // A selection that no longer exists falls back to Any.
    if (!mission_.isEmpty() && !missions.contains(mission_))
        mission_.clear();

    QStringList available;
    for (auto it = meta.cbegin(); it != meta.cend(); ++it)
        if (!owned.contains(it.key()))
            available << it.key();
    QStringList ownedList(owned.begin(), owned.end());
    sortCaseless(available);
    sortCaseless(ownedList);
    available_.clear();
    owned_.clear();
    for (const QString &n : std::as_const(available))
        if (visible(n))
            available_ << row(n);
    for (const QString &n : std::as_const(ownedList))
        if (visible(n))
            owned_ << row(n);
    emit listsChanged();
}

void BlueprintController::setFilter(QString &field, const QString &value)
{
    if (field == value)
        return;
    field = value;
    emit filtersChanged();
    rebuild();
}

void BlueprintController::setSearch(const QString &v) { setFilter(search_, v); }
void BlueprintController::setMission(const QString &v) { setFilter(mission_, v); }
void BlueprintController::setType(const QString &v) { setFilter(type_, v); }
void BlueprintController::setCls(const QString &v) { setFilter(cls_, v); }
void BlueprintController::setSize(const QString &v) { setFilter(size_, v); }
void BlueprintController::setGrade(const QString &v) { setFilter(grade_, v); }

bool BlueprintController::showTags() const
{
    return app().settings().blueprintShowTags();
}

void BlueprintController::setShowTags(bool on)
{
    app().settings().setBlueprintShowTags(on);
    rebuild();
}

bool BlueprintController::scanOtherChannels() const
{
    return app().settings().scanOtherChannels();
}

void BlueprintController::setScanOtherChannels(bool on)
{
    app().settings().setScanOtherChannels(on);
    emit optionsChanged();
}

void BlueprintController::setForceRescan(bool on)
{
    forceRescan_ = on;
    emit optionsChanged();
}

void BlueprintController::own(const QStringList &names)
{
    if (names.isEmpty())
        return;
    QSet<QString> owned = app().settings().ownedItems();
    for (const QString &n : names)
        owned.insert(n);
    app().settings().setOwnedItems(owned);
    app().recomputeOwned();
}

void BlueprintController::unown(const QStringList &names)
{
    if (names.isEmpty())
        return;
    QSet<QString> owned = app().settings().ownedItems();
    for (const QString &n : names)
        owned.remove(n);
    app().settings().setOwnedItems(owned);
    app().recomputeOwned();
}

void BlueprintController::applyOwnedTags()
{
    app().recomputeOwned();
    ownedDirty_ = false;
    emit optionsChanged();
    app().setStatus(text("blueprint_tracker.owned_tags_refreshed"));
}

// ── log scan ──────────────────────────────────────────────────────────────

void BlueprintController::scanLogs()
{
    const QString channelDir = app().paths().channelInstallDir();
    if (channelDir.isEmpty() || !QFileInfo(channelDir).isDir()) {
        app().prompts()->warning(text("enhancements.bp_scan_title"), text("enhancements.bp_scan_no_path"));
        return;
    }
    const QStringList queue = channelsToScan(app().channel(), scanOtherChannels(), app().installedChannels());
    scanNext(queue, {});
}

void BlueprintController::scanNext(QStringList queue, QSet<QString> found)
{
    if (queue.isEmpty()) {
        finishScan(found);
        return;
    }
    const QString channel = queue.takeFirst();
    const QString dir = QDir(app().installRoot()).filePath(channel);
    if (!QFileInfo(dir).isDir()) {
        qCWarning(lcApp) << "BP scan: skipping" << channel << "- no install";
        scanNext(queue, found);
        return;
    }
    const QDateTime since = forceRescan_ ? QDateTime() : app().settings().blueprintLogWatermark(channel);
    app().tasks()->run<ScanResult>(
        text("enhancements.bp_scan_title"), false,
        [dir, since](TaskRunner::Job &job) {
            job.report(text("enhancements.bp_scan_starting"));
            return scanChannel(dir, since, [&job](int done, int total, const QString &file) {
                job.report(text("enhancements.bp_scan_progress", {{QStringLiteral("file"), file}}), done, total);
            });
        },
        [this, queue, found, channel](const ScanResult &result) mutable {
            const Enclosings enc = app().enclosings();
            QSet<QString> scanned;
            for (const QString &n : result.names)
                if (const QString norm = normalizeItemName(n, enc); !norm.isEmpty())
                    scanned.insert(norm);
            const QSet<QString> &catalogue = app().knownItemNames();
            if (!catalogue.isEmpty()) {
                QSet<QString> recovered;
                QStringList unknown;
                for (const QString &n : scanned)
                    if (!catalogue.contains(n))
                        unknown << n;
                unknown.sort();
                for (const QString &n : std::as_const(unknown))
                    if (const auto real = resolveAgainstCatalogue(n, catalogue)) {
                        qCInfo(lcApp) << "BP scan: recovered" << *real << "from" << n;
                        scanned.remove(n);
                        recovered.insert(*real);
                    }
                scanned.unite(recovered);
            }
            const QSet<QString> owned = app().settings().ownedItems();
            for (const QString &n : scanned)
                if (!owned.contains(n))
                    found.insert(n);
            if (result.latestTimestamp.isValid()) {
                const QDateTime prev = app().settings().blueprintLogWatermark(channel);
                app().settings().setBlueprintLogWatermark(
                    prev.isValid() ? std::max(prev, result.latestTimestamp) : result.latestTimestamp, channel);
            }
            scanNext(queue, found);
        });
}

void BlueprintController::finishScan(const QSet<QString> &found)
{
    forceRescan_ = false;
    emit optionsChanged();
    if (found.isEmpty()) {
        app().prompts()->info(text("enhancements.bp_scan_title"), text("enhancements.bp_scan_none"));
        return;
    }
    QSet<QString> owned = app().settings().ownedItems();
    owned.unite(found);
    app().settings().setOwnedItems(owned);
    app().recomputeOwned();
    QStringList names(found.begin(), found.end());
    names.sort();
    app().prompts()->info(text("enhancements.bp_scan_title"),
                          names.size() == 1 ? text("blueprint_tracker.owned_added_singular")
                                            : text("blueprint_tracker.owned_added_plural", {{QStringLiteral("count"), static_cast<int>(names.size())}}),
                          names.join(u'\n'));
}

// ── import / export ───────────────────────────────────────────────────────

QString BlueprintController::defaultExportPath(bool csv) const
{
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(docs).filePath(text("blueprint_tracker.export_default_filename") + (csv ? QStringLiteral(".csv") : QStringLiteral(".json")));
}

void BlueprintController::exportOwned(const QUrl &target)
{
    const QSet<QString> owned = app().settings().ownedItems();
    if (owned.isEmpty()) {
        app().prompts()->info(text("blueprint_tracker.export_nothing_title"), text("blueprint_tracker.export_nothing_body"));
        return;
    }
    const QString path = target.isLocalFile() ? target.toLocalFile() : target.toString();
    const bool csv = path.endsWith(u".csv", Qt::CaseInsensitive);
    const QString body = csv ? exportOwnedBlueprintsCsv(owned, app().blueprintMeta())
                             : exportOwnedBlueprintsJson(owned, app().blueprintMeta());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(body.toUtf8()) < 0) {
        app().prompts()->error(text("blueprint_tracker.export_failed_title"),
                               text("blueprint_tracker.export_failed_body", {{QStringLiteral("error_type"), QStringLiteral("OSError")},
                                                                             {QStringLiteral("error"), f.errorString()}}));
        return;
    }
    const QString native = QDir::toNativeSeparators(path);
    app().prompts()->info(text("blueprint_tracker.export_done_title"),
                          owned.size() == 1 ? text("blueprint_tracker.export_done_singular", {{QStringLiteral("path"), native}})
                                            : text("blueprint_tracker.export_done_plural", {{QStringLiteral("count"), static_cast<int>(owned.size())},
                                                                                            {QStringLiteral("path"), native}}));
}

void BlueprintController::importOwned(const QUrl &source)
{
    const QString path = source.isLocalFile() ? source.toLocalFile() : source.toString();
    const Enclosings enc = app().enclosings();
    const auto names = parseImportNames(path, enc);
    if (!names) {
        app().prompts()->error(text("blueprint_tracker.import_invalid_title"),
                               text("blueprint_tracker.import_invalid_body", {{QStringLiteral("error"), names.error()}}));
        return;
    }
    QSet<QString> known;
    for (auto it = app().blueprintMeta().cbegin(); it != app().blueprintMeta().cend(); ++it)
        known.insert(it.key());
    const ImportMatch match = matchImportNames(*names, known, app().knownItemNames(), enc);
    QStringList skipped(match.unmatched.begin(), match.unmatched.end());
    sortCaseless(skipped);
    if (match.matched.isEmpty()) {
        app().prompts()->info(text("blueprint_tracker.import_dialog_title"),
                              text("blueprint_tracker.import_nothing_matched_body", {{QStringLiteral("count"), static_cast<int>(skipped.size())}}),
                              skipped.join(u'\n'));
        return;
    }
    QSet<QString> owned = app().settings().ownedItems();
    QSet<QString> added = match.matched - owned;
    if (!added.isEmpty()) {
        owned.unite(added);
        app().settings().setOwnedItems(owned);
        app().recomputeOwned();
    }
    QString body = added.size() == 1 ? text("blueprint_tracker.owned_added_singular")
                                     : text("blueprint_tracker.owned_added_plural", {{QStringLiteral("count"), static_cast<int>(added.size())}});
    if (!skipped.isEmpty())
        body += QStringLiteral("\n\n") + text("blueprint_tracker.import_skipped_note", {{QStringLiteral("skipped"), static_cast<int>(skipped.size())}});
    app().prompts()->info(text("blueprint_tracker.import_dialog_title"), body, skipped.join(u'\n'));
}
