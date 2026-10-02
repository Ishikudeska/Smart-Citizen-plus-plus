#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

// OneDrive can dehydrate or empty files it syncs, which has cost users their
// user.ini (#172); the app warns when its data folder is inside OneDrive.
// Ports onedrive.py.
namespace core::onedrive {

// OneDrive roots from %OneDrive%, %OneDriveConsumer%, %OneDriveCommercial%.
QStringList roots(const QProcessEnvironment &env = QProcessEnvironment::systemEnvironment());

// At or under a OneDrive root, or containing a "OneDrive" / "OneDrive - Org"
// path segment.
bool isOneDrivePath(const QString &path, const QProcessEnvironment &env = QProcessEnvironment::systemEnvironment());

// %USERPROFILE%\Documents\<appName>: the real local Documents even when the
// shell's Documents is redirected into OneDrive.
QString suggestLocalDataDir(const QString &appName,
                            const QProcessEnvironment &env = QProcessEnvironment::systemEnvironment());

} // namespace core::onedrive
