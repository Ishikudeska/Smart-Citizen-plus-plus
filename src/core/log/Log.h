#pragma once

#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>

#include <QtLogging>

#include <deque>
#include <memory>

// Logging: one Qt message handler feeds a ring buffer of recent lines (for
// the Log page and crash reports), the session's log file, and a signal the
// UI listens to. Lines keep Smart Citizen's layout:
// "2026-10-01 12:00:00,123 - category - LEVEL - message".
namespace core::log {

inline constexpr qsizetype kRingSize = 5000; // ~10 minutes of normal operation

// "INFO", "WARNING", ... as Python's logging names them.
QString levelName(QtMsgType type);
QString formatLine(const QDateTime &time, QtMsgType type, const QString &category, const QString &message);

class LogHub : public QObject
{
    Q_OBJECT

public:
    static LogHub &instance();

    // Installs the message handler (once; later calls only change the file
    // and the debug filter). `logFile`, when given, starts afresh, keeping
    // the previous session's as "<name>.previous". Debug output is filtered
    // out unless `debug`.
    void install(const QString &logFile = {}, bool debug = false);
    bool isInstalled() const;
    QString logFile() const;

    QStringList recentLines() const;
    void clearRecent();

    // What the handler does with each message; public for tests.
    void record(QtMsgType type, const QString &category, const QString &message);

signals:
    // Emitted on the logging thread: connect with a queued connection.
    void lineLogged(const QString &line, int type);

private:
    LogHub() = default;

    mutable QMutex mutex_;
    std::deque<QString> ring_;
    std::unique_ptr<QFile> file_;
    bool installed_ = false;
};

} // namespace core::log
