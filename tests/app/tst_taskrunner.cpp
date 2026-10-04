#include "TaskRunner.h"

#include <QDeadlineTimer>
#include <QSignalSpy>
#include <QTest>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <stdexcept>

// The job queue behind the progress overlay: one job at a time, results on
// the GUI thread, cancellation, and failures surfaced.
class TestTaskRunner : public QObject
{
    Q_OBJECT

private slots:
    void runsJobsInOrderOffTheGuiThread()
    {
        TaskRunner runner;
        QStringList order;
        std::atomic<bool> offGui = true;
        QThread *const gui = QThread::currentThread();
        for (const QString &name : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")})
            runner.run<QString>(
                name, false,
                [name, gui, &offGui](TaskRunner::Job &) {
                    if (QThread::currentThread() == gui)
                        offGui = false;
                    return name;
                },
                [&order, gui](QString result) {
                    QCOMPARE(QThread::currentThread(), gui);
                    order << result;
                });
        QCOMPARE(runner.queued(), 2);
        QTRY_VERIFY(!runner.running());
        QCOMPARE(order, (QStringList{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
        QVERIFY(offGui);
    }

    void cancelAllDropsTheQueueAndCancelsTheCurrentJob()
    {
        TaskRunner runner;
        std::atomic<bool> started = false;
        bool cancelledSeen = false, queuedRan = false;
        runner.run<bool>(
            QStringLiteral("long"), true,
            [&started](TaskRunner::Job &job) {
                started = true;
                QDeadlineTimer deadline(5000);
                while (!job.cancelled() && !deadline.hasExpired())
                    QThread::msleep(5);
                return job.cancelled();
            },
            [&cancelledSeen](bool cancelled) { cancelledSeen = cancelled; });
        runner.run(QStringLiteral("queued"), false, [&queuedRan](TaskRunner::Job &) { queuedRan = true; });
        QTRY_VERIFY(started);
        runner.cancelAll();
        QCOMPARE(runner.queued(), 0);
        QVERIFY(runner.cancelRequested());
        QTRY_VERIFY(!runner.running());
        QVERIFY(cancelledSeen);
        QTest::qWait(50);
        QVERIFY(!queuedRan);
    }

    // A `done` that queues two jobs must not get them run side by side.
    void jobsChainedFromDoneRunOneAtATime()
    {
        TaskRunner runner;
        std::atomic<int> active = 0, peak = 0;
        QStringList ran;
        const auto work = [&](TaskRunner::Job &) {
            const int now = ++active;
            peak = std::max(peak.load(), now);
            QThread::msleep(30);
            --active;
        };
        runner.run(QStringLiteral("first"), false, work, [&] {
            ran << QStringLiteral("first");
            runner.run(QStringLiteral("second"), false, work, [&] { ran << QStringLiteral("second"); });
            runner.run(QStringLiteral("third"), false, work, [&] { ran << QStringLiteral("third"); });
        });
        QTRY_COMPARE(ran.size(), 3);
        QTRY_VERIFY(!runner.running());
        QCOMPARE(ran, (QStringList{QStringLiteral("first"), QStringLiteral("second"), QStringLiteral("third")}));
        QCOMPARE(peak.load(), 1);
    }

    void reportsAJobThatThrows()
    {
        TaskRunner runner;
        QSignalSpy failed(&runner, &TaskRunner::failed);
        bool doneRan = false;
        runner.run<int>(
            QStringLiteral("explodes"), false,
            [](TaskRunner::Job &) -> int { throw std::runtime_error("boom"); },
            [&doneRan](int result) {
                doneRan = true;
                QCOMPARE(result, 0);
            });
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(failed.at(0).at(0).toString(), QStringLiteral("explodes"));
        QCOMPARE(failed.at(0).at(1).toString(), QStringLiteral("boom"));
        QVERIFY(doneRan);
    }
};

QTEST_GUILESS_MAIN(TestTaskRunner)
#include "tst_taskrunner.moc"
