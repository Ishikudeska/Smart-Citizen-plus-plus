#pragma once

#include <QString>
#include <QStringList>

namespace core {

// A Star Citizen install root holds at least one channel folder.
bool isScInstallRoot(const QString &path);

// True for "...\LIVE", "...\PTU" etc. (case-insensitive).
bool endsInChannel(const QString &path);

// Accepts a root or a channel folder and returns the root, or empty.
QString normalizeInstallRoot(const QString &path);

// Channels under `root` that have a Data.p4k.
QStringList installedChannels(const QString &root);

// Every install found at the usual RSI Launcher locations on every drive.
QStringList scanCommonInstallLocations();

// The install the launcher is actually maintaining: the one whose newest
// Data.p4k (across its channels) is most recent. An abandoned install keeps
// an old archive, and reading it made every later step fail (#370). Ties
// keep the given order.
QString pickLiveInstall(const QStringList &candidates);

// scanCommonInstallLocations + pickLiveInstall; empty when nothing found.
QString locateScInstall();

} // namespace core
