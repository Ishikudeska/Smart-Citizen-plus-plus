#pragma once

#include <QDateTime>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <functional>

// "Received Blueprint" events in Star Citizen's logs (#222). The game writes
// every blueprint the player receives as a notification line in the
// channel's Game.log; rotated copies pile up in <channel>\logbackups:
//
//   <2026-03-26T17:15:41.684Z> [Notice] <SHUDEvent_OnNotification> Added
//   notification "Received Blueprint: Defiance Helmet Tactical: " [23] ...
//
// Only that SHUDEvent_OnNotification line counts; the UpdateNotificationItem
// lines quoting the same text are UI echoes. Names come back raw, for the
// owned-items normalizer to fold. No settings access. Ports
// blueprint_log_scanner.py.
namespace core::blueprints {

// Blueprints did not exist before March 2026: older events and older log
// files are ignored outright.
QDateTime blueprintEpoch();

struct BlueprintEvent
{
    QDateTime timestamp; // UTC
    QString name;        // as logged, trimmed
};

// Every event in `text`, unfiltered, in order.
QList<BlueprintEvent> parseEvents(QStringView text);

// logbackups\*.log plus the live Game.log, oldest first, minus files last
// written before `since` (or the epoch when `since` is invalid).
QStringList findLogFiles(const QString &channelDir, const QDateTime &since = {});

struct ScanResult
{
    QSet<QString> names;      // raw names of events after the epoch and strictly after `since`
    QDateTime latestTimestamp; // newest event at/after the epoch, ignoring `since`; invalid when none
    int eventsMatched = 0;
    int filesScanned = 0;
};

// (files done, files total, current file name); a final (total, total, "").
using ScanProgress = std::function<void(int, int, const QString &)>;

// Reads the files line by line; an unreadable file is skipped, not fatal.
ScanResult scanFiles(const QStringList &paths, const QDateTime &since = {}, const ScanProgress &progress = {});
ScanResult scanChannel(const QString &channelDir, const QDateTime &since = {}, const ScanProgress &progress = {});

// LIVE and HOTFIX share one account's progression, so a scan of either
// also covers the other when it is installed and `includeLinked` is on.
// The active channel always comes first.
QStringList channelsToScan(const QString &activeChannel, bool includeLinked, const QStringList &installed);

} // namespace core::blueprints
