#pragma once

#include <QString>

// Crash reports. The released app has no console, so without this a crash
// is a window that vanishes with nothing to file a bug with. An unhandled
// exception, std::terminate or qFatal writes
// "<logs>/crash_YYYYMMDD_HHMMSS.log" (what happened plus the recent log
// lines) and, on Windows, a minidump beside it; then the default handling
// carries on. MinGW builds have no PDBs, which is why the log ring matters.
// Ports crash_handler.py.
namespace core::crash {

// Once; later calls do nothing.
void install(const QString &logsDir);
bool isInstalled();

// The text report; returns its path, or empty when it could not be written.
// The hooks call it; so can tests.
QString writeCrashReport(const QString &logsDir, const QString &reason, const QString &thread = {});

// For the fatal-message path: writes to the installed logs folder.
void reportFatal(const QString &reason);

} // namespace core::crash
