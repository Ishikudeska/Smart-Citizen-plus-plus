#pragma once

#include "core/util/Progress.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <deque>
#include <functional>
#include <memory>

// Runs long jobs (extraction, generation, loading, applying) on the thread
// pool, one at a time in submission order, and publishes their progress
// for the single progress overlay in QML. Replaces the Python app's eight
// QThread workers: a job is a plain function, cancellation is a flag it
// polls, and its result comes back on the GUI thread.
class TaskRunner : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("owned by App")

    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(int completed READ completed NOTIFY changed)
    Q_PROPERTY(int total READ total NOTIFY changed)
    Q_PROPERTY(bool cancellable READ cancellable NOTIFY changed)
    Q_PROPERTY(bool cancelRequested READ cancelRequested NOTIFY changed)
    Q_PROPERTY(int queued READ queued NOTIFY changed)

public:
    // What a job sees: progress reporting and the cancel flag.
    class Job
    {
    public:
        core::ProgressSink &progress() { return sink_; }
        void report(const QString &message, int completed = -1, int total = -1);
        bool cancelled() const { return cancel_.isCancelled(); }
        const std::atomic<bool> *cancelFlag() const { return cancel_.flag(); }
        const core::CancelToken &cancelToken() const { return cancel_; }

    private:
        friend class TaskRunner;
        explicit Job(core::ProgressSink::Callback callback) : sink_(std::move(callback)) {}
        core::ProgressSink sink_;
        core::CancelToken cancel_;
    };

    explicit TaskRunner(QObject *parent = nullptr);

    // Queues `work` to run off the GUI thread; `done` then gets its result on
    // the GUI thread (not called if this object is gone).
    template <class Result>
    void run(const QString &title, bool cancellable, std::function<Result(Job &)> work,
             std::function<void(Result)> done)
    {
        auto result = std::make_shared<Result>();
        enqueue({title, cancellable, [work = std::move(work), result](Job &job) { *result = work(job); },
                 [done = std::move(done), result] {
                     if (done)
                         done(std::move(*result));
                 }});
    }
    void run(const QString &title, bool cancellable, std::function<void(Job &)> work,
             std::function<void()> done = {});

    bool running() const { return current_ != nullptr; }
    QString title() const { return title_; }
    QString message() const { return message_; }
    int completed() const { return completed_; }
    int total() const { return total_; }
    bool cancellable() const { return cancellable_; }
    bool cancelRequested() const { return cancelRequested_; }
    int queued() const { return static_cast<int>(pending_.size()); }

    Q_INVOKABLE void cancel();
    // Drops the queued jobs and cancels the current one (if it can be).
    void cancelAll();

signals:
    void runningChanged();
    void changed();
    void finished(const QString &title);
    // The job threw; `done` still ran, with a default-constructed result.
    void failed(const QString &title, const QString &message);

private:
    struct Pending
    {
        QString title;
        bool cancellable = false;
        std::function<void(Job &)> work;
        std::function<void()> done;
    };

    void enqueue(Pending task);
    void startNext();
    void onProgress(int completed, int total, const QString &message);

    std::deque<Pending> pending_;
    std::shared_ptr<Job> current_;
    QString title_, message_;
    int completed_ = 0, total_ = 0;
    bool cancellable_ = false, cancelRequested_ = false;
};
