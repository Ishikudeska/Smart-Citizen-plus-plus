#pragma once

#include <QString>

namespace core {

class Settings;

// Machine locations the paths are built from. defaults() reads them from
// the environment; tests substitute temporary folders.
struct PathRoots
{
    bool portable = false;
    QString portableRoot;  // <exe dir>/data
    QString documentsDir;  // shell Documents (honours OneDrive redirection)
    QString localAppData;  // %LOCALAPPDATA%

    static PathRoots defaults();
};

// Where everything lives for the active channel and language.
//
//   user data      Documents\<app>\                 (or override, or <exe>\data\)
//     <channel>\user.ini, backups\, cache\base.ini, cache\*_enhancements.ini
//     <channel>\cache\lang\<language>\base.ini      non-English languages
//     logs\                                         not per-channel
//   DataForge      %LOCALAPPDATA%\<app>\<channel>\cache\dataforge\   (or override)
//   game           <install root>\<channel>\Data.p4k, \user.cfg,
//                  \data\Localization\<sc language id>\global.ini
//
// Getters only compute; whoever writes a file creates its folder.
class Paths
{
public:
    Paths(const Settings &settings, PathRoots roots = PathRoots::defaults());

    QString userDataRoot() const;
    QString channelDataDir() const;
    QString cacheDir() const;
    QString baseIni(const QString &language = {}) const;
    QString enhancementsDir(const QString &language = {}) const;
    QString userIni() const;
    QString backupsDir() const;
    QString logsDir() const;
    QString dataForgeCacheDir() const;

    // Empty when no install root is configured.
    QString channelInstallDir() const;
    QString p4kPath() const;
    QString gameGlobalIni(const QString &language = {}) const;

private:
    QString language(const QString &requested) const;

    const Settings &settings_;
    PathRoots roots_;
};

} // namespace core
