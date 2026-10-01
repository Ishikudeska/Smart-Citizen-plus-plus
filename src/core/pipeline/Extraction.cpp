#include "core/pipeline/Extraction.h"

#include "engine/Try.h"
#include "engine/forge/DataForge.h"
#include "engine/forge/Exporter.h"
#include "engine/io/FileSystem.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace core {

using engine::Errc;
using engine::fail;

namespace {

std::filesystem::path fsPath(const QString &path)
{
    return std::filesystem::path(path.toStdU16String());
}

std::optional<std::size_t> findEntry(const engine::p4k::Archive &archive, const std::function<bool(std::string_view)> &match)
{
    for (std::size_t i = 0; i < archive.entryCount(); ++i)
        if (match(archive.name(i)))
            return i;
    return std::nullopt;
}

bool iendsWith(std::string_view s, std::string_view suffix)
{
    if (s.size() < suffix.size())
        return false;
    for (std::size_t i = 0; i < suffix.size(); ++i) {
        char a = s[s.size() - suffix.size() + i];
        if (a >= 'A' && a <= 'Z')
            a = static_cast<char>(a + 32);
        if (a != suffix[i])
            return false;
    }
    return true;
}

} // namespace

const QStringList &dataForgeKeepSubpaths()
{
    static const QStringList subpaths = {
        QStringLiteral("entities/scitem"),
        QStringLiteral("entities/spaceships"),
        QStringLiteral("entities/missions"),
        QStringLiteral("entities/contracts"),
        QStringLiteral("entities/jobterminal"),
        QStringLiteral("contracts/contractgenerator"),
        QStringLiteral("contracts/contracttemplates"),
        QStringLiteral("crafting/blueprintrewards"),
        QStringLiteral("crafting/blueprints/crafting"),
        QStringLiteral("missionbroker/pu_missions"),
        QStringLiteral("ammoparams/vehicle"),
        QStringLiteral("ammoparams/fps"),
        QStringLiteral("reputation/rewards/missionrewards_reputation"),
        QStringLiteral("reputation/standings"),
    };
    return subpaths;
}

std::optional<P4kStamp> P4kStamp::of(const QString &p4kPath)
{
    const QFileInfo info(p4kPath);
    if (!info.exists())
        return std::nullopt;
    return P4kStamp{info.size(), info.lastModified().toSecsSinceEpoch()};
}

std::optional<P4kStamp> P4kStamp::read(const QString &stampFile)
{
    QFile file(stampFile);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QList<QByteArray> parts = file.readAll().trimmed().split(':');
    if (parts.size() != 2)
        return std::nullopt;
    bool okSize = false, okTime = false;
    P4kStamp stamp{parts[0].toLongLong(&okSize), parts[1].toLongLong(&okTime)};
    if (!okSize || !okTime)
        return std::nullopt;
    return stamp;
}

bool P4kStamp::write(const QString &stampFile) const
{
    QDir().mkpath(QFileInfo(stampFile).absolutePath());
    QSaveFile file(stampFile);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(key().toUtf8());
    return file.commit();
}

QString P4kStamp::key() const
{
    return QStringLiteral("%1:%2").arg(size).arg(mtimeSecs);
}

QString dataForgeRecordsDir(const QString &cacheDir)
{
    return QDir(cacheDir).filePath(QStringLiteral("raw/libs/foundry/records"));
}

engine::Result<void> extractBaseIni(const engine::p4k::Archive &archive, const QString &baseIniPath)
{
    const auto index = archive.find("Data/Localization/english/global.ini");
    if (!index)
        return fail(Errc::NotFound, "Data/Localization/english/global.ini is not in " + archive.path().string());
    auto bytes = archive.read(*index);
    if (!bytes)
        return std::unexpected(bytes.error());
    SC_TRY(engine::io::createDirectories(fsPath(QFileInfo(baseIniPath).absolutePath())));
    SC_TRY(engine::io::writeFile(fsPath(baseIniPath), *bytes));
    if (const auto stamp = P4kStamp::of(QString::fromStdU16String(archive.path().u16string())))
        stamp->write(QFileInfo(baseIniPath).dir().filePath(kP4kStampName));
    return {};
}

engine::Result<DataForgeExtraction> extractDataForge(const engine::p4k::Archive &archive, const QString &cacheDir,
                                                     const QString &patchRoot, const StepProgress &progress,
                                                     const std::atomic<bool> *cancel)
{
    auto report = [&](const QString &step, qint64 done, qint64 total) {
        if (progress)
            progress(step, done, total);
    };

    // The database is Data/Game2.dcb today (Game.dcb in older builds).
    const auto dcb = findEntry(archive, [](std::string_view name) { return iendsWith(name, ".dcb"); });
    if (!dcb)
        return fail(Errc::NotFound, "no DataForge database (.dcb) in " + archive.path().string());

    report(QStringLiteral("Reading %1").arg(QString::fromUtf8(archive.name(*dcb))), 0, 1);
    auto bytes = archive.read(*dcb);
    if (!bytes)
        return std::unexpected(bytes.error());
    auto forge = engine::forge::DataForge::load(std::move(*bytes));
    if (!forge)
        return std::unexpected(forge.error());

    const QString raw = QDir(cacheDir).filePath(QStringLiteral("raw"));
    const QString staging = raw + QStringLiteral(".new");
    const QString previous = raw + QStringLiteral(".old");
    SC_TRY(engine::io::removeAll(fsPath(staging)));

    QStringList prefixes;
    for (const QString &sub : dataForgeKeepSubpaths())
        prefixes << QStringLiteral("libs/foundry/records/") + sub + u'/';
    std::vector<std::string> stdPrefixes;
    for (const QString &p : prefixes)
        stdPrefixes.push_back(p.toStdString());

    engine::forge::ExportOptions options;
    options.include = [&stdPrefixes](std::string_view path) {
        for (const std::string &p : stdPrefixes)
            if (path.starts_with(p))
                return true;
        return false;
    };
    options.cancel = cancel;
    options.progress = [&](std::size_t done, std::size_t total) {
        report(QStringLiteral("Converting DataForge records"), static_cast<qint64>(done), static_cast<qint64>(total));
    };
    auto stats = engine::forge::exportRecords(*forge, fsPath(staging), options);
    if (!stats) {
        engine::io::removeAll(fsPath(staging));
        return std::unexpected(stats.error());
    }
    if (!stats->failures.empty()) {
        engine::io::removeAll(fsPath(staging));
        return fail(Errc::Io, "could not write the DataForge cache: " + stats->failures.front());
    }

    // Swap the new tree in; the old one is kept until the swap succeeds.
    report(QStringLiteral("Replacing the DataForge cache"), 0, 1);
    SC_TRY(engine::io::removeAll(fsPath(previous)));
    if (QFileInfo::exists(raw) && !QDir().rename(raw, previous)) {
        engine::io::removeAll(fsPath(staging));
        return fail(Errc::Io, "cannot replace " + raw.toStdString() + " (is a file in it open?)");
    }
    if (!QDir().rename(staging, raw)) {
        QDir().rename(previous, raw);
        return fail(Errc::Io, "cannot move the new DataForge cache into " + raw.toStdString());
    }
    engine::io::removeAll(fsPath(previous));

    DataForgeExtraction result;
    result.records = stats->written;
    report(QStringLiteral("Applying data patches"), 0, 1);
    result.patches = applyPatches(patchRoot, dataForgeRecordsDir(cacheDir));

    if (const auto stamp = P4kStamp::of(QString::fromStdU16String(archive.path().u16string())))
        stamp->write(QDir(cacheDir).filePath(kP4kStampName));
    report(QStringLiteral("Done"), 1, 1);
    return result;
}

bool baseIniIsFresh(const QString &p4kPath, const QString &baseIniPath)
{
    const auto current = P4kStamp::of(p4kPath);
    const auto stamped = P4kStamp::read(QFileInfo(baseIniPath).dir().filePath(kP4kStampName));
    return current && stamped && QFileInfo::exists(baseIniPath) && current->size == stamped->size;
}

bool dataForgeCacheIsFresh(const QString &p4kPath, const QString &cacheDir)
{
    const auto current = P4kStamp::of(p4kPath);
    const auto stamped = P4kStamp::read(QDir(cacheDir).filePath(kP4kStampName));
    if (!current || !stamped || current->size != stamped->size)
        return false;
    // A stamp without content (an interrupted first run) is not fresh.
    QDirIterator it(dataForgeRecordsDir(cacheDir), {QStringLiteral("*.xml")}, QDir::Files, QDirIterator::Subdirectories);
    return it.hasNext();
}

} // namespace core
