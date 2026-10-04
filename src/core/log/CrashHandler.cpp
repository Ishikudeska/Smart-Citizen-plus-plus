#include "core/log/CrashHandler.h"

#include "core/AppIdentity.h"
#include "core/log/Log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSysInfo>
#include <QThread>

#include <atomic>
#include <exception>

#ifdef Q_OS_WIN
#include <windows.h>
// dbghelp.h needs windows.h first.
#include <dbghelp.h>
#endif

namespace core::crash {

namespace {

std::atomic<bool> g_installed = false;
std::atomic<bool> g_reporting = false; // one report per crash, even if the report itself faults
// Set once by install(), read by the handlers. A function-local static, so
// it is built on first use instead of during static initialization.
QString &crashLogsDir()
{
    static QString dir;
    return dir;
}
std::terminate_handler g_previousTerminate = nullptr;

QString stamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
}

QString currentThreadName()
{
    const QString name = QThread::currentThread() ? QThread::currentThread()->objectName() : QString();
    if (!name.isEmpty())
        return name;
    return QThread::isMainThread() ? QStringLiteral("MainThread")
                                   : QStringLiteral("Thread 0x%1").arg(quintptr(QThread::currentThreadId()), 0, 16);
}

#ifdef Q_OS_WIN
void writeMinidump(const QString &path, EXCEPTION_POINTERS *info)
{
    const HANDLE file = CreateFileW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(path).utf16()),
                                    GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    MINIDUMP_EXCEPTION_INFORMATION exception{GetCurrentThreadId(), info, FALSE};
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                      MINIDUMP_TYPE(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory),
                      info ? &exception : nullptr, nullptr, nullptr);
    CloseHandle(file);
}

LONG WINAPI unhandledException(EXCEPTION_POINTERS *info)
{
    if (!g_reporting.exchange(true)) {
        const QString reason = QStringLiteral("Unhandled exception 0x%1 at 0x%2")
                                   .arg(info->ExceptionRecord->ExceptionCode, 8, 16, QChar(u'0'))
                                   .arg(quintptr(info->ExceptionRecord->ExceptionAddress), 0, 16);
        const QString report = writeCrashReport(crashLogsDir(), reason, currentThreadName());
        if (!report.isEmpty())
            writeMinidump(report.chopped(4) + QStringLiteral(".dmp"), info);
    }
    return EXCEPTION_CONTINUE_SEARCH; // let Windows Error Reporting finish the job
}
#endif

void onTerminate()
{
    if (!g_reporting.exchange(true)) {
        QString reason = QStringLiteral("std::terminate");
        if (const std::exception_ptr current = std::current_exception()) {
            try {
                std::rethrow_exception(current);
            } catch (const std::exception &e) {
                reason += QStringLiteral(": uncaught exception: ") + QString::fromUtf8(e.what());
            } catch (...) {
                reason += QStringLiteral(": uncaught non-standard exception");
            }
        }
        const QString report = writeCrashReport(crashLogsDir(), reason, currentThreadName());
#ifdef Q_OS_WIN
        if (!report.isEmpty())
            writeMinidump(report.chopped(4) + QStringLiteral(".dmp"), nullptr);
#endif
    }
    if (g_previousTerminate)
        g_previousTerminate();
    std::abort();
}

} // namespace

void install(const QString &logsDir)
{
    if (g_installed.exchange(true))
        return;
    crashLogsDir() = logsDir;
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(unhandledException);
#endif
    g_previousTerminate = std::set_terminate(onTerminate);
}

bool isInstalled()
{
    return g_installed;
}

QString writeCrashReport(const QString &logsDir, const QString &reason, const QString &thread)
{
    if (logsDir.isEmpty() || !QDir().mkpath(logsDir))
        return QString();
    const QString when = stamp();
    const QString path = QDir(logsDir).filePath(QStringLiteral("crash_%1.log").arg(when));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return QString();
    const QStringList lines = log::LogHub::instance().recentLines();
    QString text;
    text += QStringLiteral("%1 crash dump - %2\n").arg(QString::fromUtf8(identity::kAppName), when);
    text += QStringLiteral("Version: %1\n").arg(QString::fromUtf8(identity::kVersion));
    text += QStringLiteral("Thread: %1\n").arg(thread.isEmpty() ? currentThreadName() : thread);
    text += QStringLiteral("Platform: %1 (%2)\n\n").arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture());
    text += QStringLiteral("--- What happened ---\n%1\n\n").arg(reason);
    text += QStringLiteral("--- Recent log (%1 lines) ---\n").arg(lines.size());
    for (const QString &line : lines)
        text += line + u'\n';
    file.write(text.toUtf8());
    return file.flush() ? path : QString();
}

void reportFatal(const QString &reason)
{
    if (g_installed && !g_reporting.exchange(true))
        writeCrashReport(crashLogsDir(), reason);
}

} // namespace core::crash
