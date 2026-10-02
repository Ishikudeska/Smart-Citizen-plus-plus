#pragma once

#include <QElapsedTimer>
#include <QString>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <tuple>

// Progress and cancellation for work that fans out over threads. Ports
// progress_sink.py and perf.py.
namespace core {

// Workers call advance() from any thread; updates are coalesced into one
// (completed, total, message) and passed to the callback at most every
// `minInterval`, except that total changes, flush() and reaching the total
// always report. Identical repeats are dropped.
class ProgressSink
{
public:
    using Callback = std::function<void(int completed, int total, const QString &message)>;

    explicit ProgressSink(Callback callback = {}, int total = 0,
                          std::chrono::milliseconds minInterval = std::chrono::milliseconds(50));

    void setTotal(int total);
    void addTotal(int delta);
    void advance(int delta = 1, const std::optional<QString> &message = std::nullopt);
    void setMessage(const QString &message);
    std::tuple<int, int, QString> snapshot() const;
    void flush();

private:
    void emitProgress(bool force);

    Callback callback_;
    mutable std::mutex mutex_;
    int completed_ = 0;
    int total_ = 0;
    QString message_;
    std::chrono::milliseconds minInterval_;
    QElapsedTimer clock_;
    qint64 lastEmitMs_ = -1;
    std::optional<std::tuple<int, int, QString>> lastEmitted_;
};

// A shareable cancel flag; the engine takes flag().
class CancelToken
{
public:
    CancelToken() : flag_(std::make_shared<std::atomic<bool>>(false)) {}

    void cancel() const { flag_->store(true); }
    bool isCancelled() const { return flag_->load(); }
    const std::atomic<bool> *flag() const { return flag_.get(); }

private:
    std::shared_ptr<std::atomic<bool>> flag_;
};

// Logs "<name>: 1.234s" to the scx.perf category (debug) when it goes out of
// scope. Costs nothing measurable when debug logging is off.
class PerfTimer
{
public:
    explicit PerfTimer(const char *name);
    ~PerfTimer();
    PerfTimer(const PerfTimer &) = delete;
    PerfTimer &operator=(const PerfTimer &) = delete;

private:
    const char *name_;
    QElapsedTimer timer_;
};

} // namespace core
