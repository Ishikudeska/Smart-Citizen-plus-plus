#include "TaskRunner.h"

#include <QLoggingCategory>
#include <QThreadPool>

Q_LOGGING_CATEGORY(lcTasks, "scx.tasks")

void TaskRunner::Job::report(const QString &message, int completed, int total)
{
    if (total >= 0)
        sink_.setTotal(total);
    if (completed >= 0) {
        const auto [done, all, msg] = sink_.snapshot();
        Q_UNUSED(all)
        Q_UNUSED(msg)
        sink_.advance(completed - done, message);
    } else {
        sink_.setMessage(message);
    }
    sink_.flush();
}

TaskRunner::TaskRunner(QObject *parent) : QObject(parent) {}

void TaskRunner::run(const QString &title, bool cancellable, std::function<void(Job &)> work,
                     std::function<void()> done)
{
    enqueue({title, cancellable, std::move(work), std::move(done)});
}

void TaskRunner::cancel()
{
    if (current_ && cancellable_ && !cancelRequested_) {
        current_->cancel_.cancel();
        cancelRequested_ = true;
        qCInfo(lcTasks) << "cancel requested:" << title_;
        emit changed();
    }
}

void TaskRunner::enqueue(Pending task)
{
    pending_.push_back(std::move(task));
    if (!current_)
        startNext();
    else
        emit changed();
}

void TaskRunner::startNext()
{
    if (pending_.empty())
        return;
    Pending task = std::move(pending_.front());
    pending_.pop_front();

    QPointer<TaskRunner> self(this);
    current_ = std::shared_ptr<Job>(new Job([self](int completed, int total, const QString &message) {
        QMetaObject::invokeMethod(
            self.data(),
            [self, completed, total, message] {
                if (self)
                    self->onProgress(completed, total, message);
            },
            Qt::QueuedConnection);
    }));
    title_ = task.title;
    message_.clear();
    completed_ = total_ = 0;
    cancellable_ = task.cancellable;
    cancelRequested_ = false;
    emit runningChanged();
    emit changed();
    qCInfo(lcTasks) << "start:" << title_;

    std::shared_ptr<Job> job = current_;
    auto work = std::make_shared<std::function<void(Job &)>>(std::move(task.work));
    auto done = std::make_shared<std::function<void()>>(std::move(task.done));
    QThreadPool::globalInstance()->start([self, job, work, done] {
        try {
            (*work)(*job);
        } catch (const std::exception &e) {
            qCCritical(lcTasks) << "task failed:" << e.what();
        }
        QMetaObject::invokeMethod(
            self.data(),
            [self, job, done] {
                if (!self)
                    return;
                const QString title = self->title_;
                self->current_.reset();
                self->title_.clear();
                self->message_.clear();
                emit self->runningChanged();
                emit self->changed();
                qCInfo(lcTasks) << "done:" << title;
                if (*done)
                    (*done)();
                emit self->finished(title);
                self->startNext();
            },
            Qt::QueuedConnection);
    });
}

void TaskRunner::onProgress(int completed, int total, const QString &message)
{
    if (!current_)
        return;
    completed_ = completed;
    total_ = total;
    if (!message.isEmpty())
        message_ = message;
    emit changed();
}
