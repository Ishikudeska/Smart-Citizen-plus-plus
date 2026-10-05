#include "core/apply/ApplyService.h"

#include "core/apply/Backups.h"
#include "core/apply/GameFile.h"
#include "core/apply/Stamps.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace core {

IniMap userOverridesFrom(const QList<StringEntry> &entries)
{
    IniMap overrides;
    for (const StringEntry &e : entries)
        if (!e.customValue.isEmpty())
            overrides.insert(e.key, e.customValue);
    return overrides;
}

ApplyOutcome applyToGame(const ApplyInputs &in)
{
    ApplyOutcome out;
    if (!QFileInfo::exists(in.baseIniPath)) {
        out.error = QStringLiteral("No base.ini at %1. Extract it from Data.p4k first.").arg(in.baseIniPath);
        return out;
    }

    const GameFileBackups backups(in.backupsDir);
    QString backupError;
    out.backupPath = backups.backup(in.gameFile, &backupError);
    if (!backupError.isEmpty()) {
        out.error = backupError;
        return out;
    }

    SourceMap sources = in.sources.sources;
    if (!in.excludeEnhancementKeys.isEmpty())
        if (const auto enh = sources.find(kSourceEnhancements); enh != sources.end()) {
            IniMap kept;
            for (const auto &[key, value] : enh->second)
                if (!in.excludeEnhancementKeys.contains(key))
                    kept.insert(key, value);
            enh->second = std::move(kept);
        }
    IniMap merged = mergeSourcesByHierarchy(sources, in.sources.hierarchy, &in.userOverrides);
    if (in.beforeStamps)
        in.beforeStamps(merged);

    const auto global = in.sources.sources.find(kSourceGlobal);
    const IniMap stock = global == in.sources.sources.end() ? IniMap() : global->second;
    stampJournalEntries(merged, stock, in.appName, in.version);
    stampFrontendVersion(merged, in.appName, in.version);

    if (const QString error = writeGameFile(in.baseIniPath, merged, in.gameFile); !error.isEmpty()) {
        out.error = error;
        return out;
    }

    QSet<QString> stockKeys;
    const IniMap &keySource = stock.isEmpty() ? loadIni(in.baseIniPath) : stock;
    stockKeys.reserve(keySource.size());
    for (const auto &[key, value] : keySource)
        stockKeys.insert(key);
    out.validation = validateGameFile(in.gameFile, stockKeys);
    if (!out.validation.isEmpty()) {
        QFile::remove(in.gameFile);
        if (!out.backupPath.isEmpty())
            out.restoredBackup = GameFileBackups::restore(out.backupPath, in.gameFile).isEmpty();
        return out;
    }

    if (!in.languagesIniSource.isEmpty() && !in.languagesIniDest.isEmpty() &&
        QFileInfo::exists(in.languagesIniSource)) {
        QDir().mkpath(QFileInfo(in.languagesIniDest).absolutePath());
        QFile::remove(in.languagesIniDest);
        if (!QFile::copy(in.languagesIniSource, in.languagesIniDest))
            qWarning("could not copy languages.ini to %s", qPrintable(in.languagesIniDest));
    }
    if (!in.channelInstallDir.isEmpty())
        out.userCfg = ensureUserCfgLanguage(in.channelInstallDir, in.scLanguageId);
    out.ok = true;
    return out;
}

} // namespace core
