#pragma once

#include "core/apply/UserCfg.h"
#include "core/merge/SourceLoader.h"
#include "core/model/StringEntry.h"
#include "core/text/IniFile.h"

#include <QList>
#include <QSet>
#include <QString>

#include <functional>

namespace core {

struct ApplyInputs
{
    LoadedSources sources; // reloaded from disk just before applying
    IniMap userOverrides;  // see userOverridesFrom()
    QString baseIniPath;   // stock base.ini: line structure and validation keys
    QString gameFile;      // <channel>\data\Localization\<lang>\global.ini
    QString backupsDir;
    QString channelInstallDir; // for user.cfg; empty skips it
    QString scLanguageId;
    QString appName;
    QString version;
    // Enhancement keys left out of the merge: the "New" lines (not in the
    // stock base.ini) unless the user opted to include them.
    QSet<QString> excludeEnhancementKeys;
    // languages.ini shipped for the selected language, copied to
    // <channel>\data\languages.ini when both are set.
    QString languagesIniSource;
    QString languagesIniDest;
    // Runs on the merged strings before the stamps (e.g. weaving [Owned]
    // into blueprint lists).
    std::function<void(IniMap &)> beforeStamps;
};

struct ApplyOutcome
{
    bool ok = false;
    QString error;      // backup or write failure
    QString validation; // why the written file was rejected (then rolled back)
    QString backupPath; // the backup taken this run, if any
    bool restoredBackup = false;
    UserCfgResult userCfg = UserCfgResult::Failed;
};

// Every entry with a custom value, in table order.
IniMap userOverridesFrom(const QList<StringEntry> &entries);

// Writes the game's global.ini. Ports MainWindow.apply_to_game without the
// UI: back up the current file (5 kept), merge the sources with the user's
// overrides on top, stamp journals and the main-menu version line, write in
// base.ini's line order with a BOM, and validate the result. A file that
// fails validation is removed and the backup put back. Finally user.cfg is
// pointed at the selected language. The caller saves user.ini first, so a
// failed game write never loses edits.
ApplyOutcome applyToGame(const ApplyInputs &in);

} // namespace core
