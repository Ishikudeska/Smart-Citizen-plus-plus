#include "core/util/Progress.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcPerf, "scx.perf")

namespace core {

ProgressSink::ProgressSink(Callback callback, int total, std::chrono::milliseconds minInterval)
    : callback_(std::move(callback)), total_(total), minInterval_(minInterval)
{
    clock_.start();
}

void ProgressSink::setTotal(int total)
{
    {
        std::lock_guard lock(mutex_);
        total_ = total;
    }
    emitProgress(true);
}

void ProgressSink::addTotal(int delta)
{
    {
        std::lock_guard lock(mutex_);
        total_ += delta;
    }
    emitProgress(true);
}

void ProgressSink::advance(int delta, const std::optional<QString> &message)
{
    {
        std::lock_guard lock(mutex_);
        completed_ += delta;
        if (message)
            message_ = *message;
    }
    emitProgress(false);
}

void ProgressSink::setMessage(const QString &message)
{
    {
        std::lock_guard lock(mutex_);
        message_ = message;
    }
    emitProgress(false);
}

std::tuple<int, int, QString> ProgressSink::snapshot() const
{
    std::lock_guard lock(mutex_);
    return {completed_, total_, message_};
}

void ProgressSink::flush()
{
    emitProgress(true);
}

void ProgressSink::emitProgress(bool force)
{
    if (!callback_)
        return;
    std::tuple<int, int, QString> payload;
    {
        std::lock_guard lock(mutex_);
        payload = {completed_, total_, message_};
        const bool done = total_ > 0 && completed_ >= total_;
        const qint64 now = clock_.elapsed();
        if (!force && !done) {
            if (lastEmitMs_ >= 0 && now - lastEmitMs_ < minInterval_.count())
                return;
            if (lastEmitted_ == payload)
                return;
        }
        lastEmitMs_ = now;
        lastEmitted_ = payload;
    }
    // Outside the lock: the callback may post to the UI or call back in.
    std::apply(callback_, payload);
}

PerfTimer::PerfTimer(const char *name) : name_(name)
{
    if (lcPerf().isDebugEnabled())
        timer_.start();
}

PerfTimer::~PerfTimer()
{
    if (timer_.isValid())
        qCDebug(lcPerf, "%s: %.3fs", name_, double(timer_.nsecsElapsed()) / 1e9);
}

} // namespace core
