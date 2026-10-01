// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <ai/AiEnums.h>
#include <ai/Errors.h>
#include <ai/JobHandler.h>
#include <ai/JobQueue.h>
#include <common/ManualClock.h>
#include <library/Database.h>
#include <library/Migrator.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>

using linernotes::ai::JobHandler;
using linernotes::ai::JobInfo;
using linernotes::ai::JobQueue;
using linernotes::ai::JobState;
using linernotes::ai::roughTokenCount;
using linernotes::ai::TokenUsage;
using linernotes::library::Database;
using linernotes::library::Migrator;
using linernotes::test::ManualClock;
namespace errc = linernotes::ai::errc;
namespace core = linernotes::core;

namespace {

enum class CallbackBehavior : std::uint8_t {
    AsyncSuccess,
    AsyncError,
    Never,
};

struct FakeHandlerState {
    QStringList processedKeys;
    int currentInFlight = 0;
    int peakInFlight = 0;
    CallbackBehavior defaultBehavior = CallbackBehavior::AsyncSuccess;
    QHash<QString, CallbackBehavior> behaviorMap;
    QHash<QString, core::Result<void>> errorMap;
};

class FakeHandler : public JobHandler {
public:
    explicit FakeHandler(QString kind = QStringLiteral("test.fake"), int maxInFlight = 2)
        : m_kind(std::move(kind))
        , m_maxInFlight(maxInFlight)
    {
    }

    [[nodiscard]] QString kind() const override { return m_kind; }
    [[nodiscard]] int maxInFlight() const override { return m_maxInFlight; }

    [[nodiscard]] TokenUsage estimate(
        const QString &itemKey, const QJsonObject & /*params*/) const override
    {
        return TokenUsage {
            .promptTokens = roughTokenCount(itemKey),
            .completionTokens = 10,
        };
    }

    std::unique_ptr<QObject> process(const QString &itemKey, const QJsonObject & /*params*/,
        std::function<void(const core::Result<void> &)> done) override
    {
        m_state.processedKeys.append(itemKey);
        ++m_state.currentInFlight;
        m_state.peakInFlight = std::max(m_state.peakInFlight, m_state.currentInFlight);

        auto work = std::make_unique<QObject>();
        auto *workPtr = work.get();
        auto inFlightFlag = std::make_shared<bool>(true);

        QObject::connect(workPtr, &QObject::destroyed, [this, inFlightFlag]() {
            if (*inFlightFlag) {
                *inFlightFlag = false;
                --m_state.currentInFlight;
            }
        });

        const CallbackBehavior behavior
            = m_state.behaviorMap.value(itemKey, m_state.defaultBehavior);

        if (behavior == CallbackBehavior::Never) {
            return work;
        }

        const auto result = (behavior == CallbackBehavior::AsyncError)
            ? m_state.errorMap.value(itemKey,
                  core::Error {
                      .code = QString(errc::kHttp),
                      .message = QStringLiteral("HTTP error"),
                      .detail = { },
                  })
            : core::Result<void>();

        QTimer::singleShot(0, [this, inFlightFlag, done, result]() {
            if (*inFlightFlag) {
                *inFlightFlag = false;
                --m_state.currentInFlight;
            }
            done(result);
        });

        return work;
    }

    FakeHandlerState &state() { return m_state; }

private:
    FakeHandlerState m_state;
    QString m_kind;
    int m_maxInFlight = 2;
};

class TstJobQueue : public QObject {
    Q_OBJECT

private slots:
    void runsAllItems();
    void pauseAndResume();
    void resumesAfterRestart();
    void failuresAndConfigErrors();
    void cancelAbortsInFlight();
    void estimateSumsAndRoughTokenCount();
    void concurrentWritesDoNotBlockJob();
};

void TstJobQueue::runsAllItems()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    ManualClock clock(1000);
    JobQueue queue(db, clock);

    auto handler = std::make_unique<FakeHandler>(QStringLiteral("test.fake"), 2);
    auto *handlerPtr = handler.get();
    queue.registerHandler(std::move(handler));

    QSignalSpy spy(&queue, &JobQueue::jobChanged);

    const QStringList items = {
        QStringLiteral("item1"),
        QStringLiteral("item2"),
        QStringLiteral("item3"),
        QStringLiteral("item4"),
        QStringLiteral("item5"),
    };

    const auto enqueueRes
        = queue.enqueue(QStringLiteral("test.fake"), QStringLiteral("Test Job"), items);
    QVERIFY(enqueueRes.ok());
    const qint64 jobId = enqueueRes.value();

    QTRY_COMPARE_WITH_TIMEOUT(
        queue.job(jobId).value_or(JobInfo { }).state, JobState::Completed, 5000);

    const auto jobOpt = queue.job(jobId);
    QVERIFY(jobOpt.has_value());
    if (!jobOpt.has_value()) {
        return;
    }

    QCOMPARE(jobOpt->state, JobState::Completed);
    QCOMPARE(jobOpt->total, 5);
    QCOMPARE(jobOpt->done, 5);
    QCOMPARE(jobOpt->failed, 0);
    QCOMPARE(handlerPtr->state().peakInFlight, 2);
    QCOMPARE(handlerPtr->state().processedKeys, items);
    QVERIFY(spy.count() >= 5);
}

void TstJobQueue::pauseAndResume()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    ManualClock clock(1000);
    JobQueue queue(db, clock);

    auto handler = std::make_unique<FakeHandler>(QStringLiteral("test.fake"), 2);
    auto *handlerPtr = handler.get();
    queue.registerHandler(std::move(handler));

    const QStringList items = {
        QStringLiteral("item1"),
        QStringLiteral("item2"),
        QStringLiteral("item3"),
        QStringLiteral("item4"),
        QStringLiteral("item5"),
    };

    const auto enqueueRes
        = queue.enqueue(QStringLiteral("test.fake"), QStringLiteral("Test Job"), items);
    QVERIFY(enqueueRes.ok());
    const qint64 jobId = enqueueRes.value();

    // Immediately pause
    queue.pause(jobId);

    // In-flight items (item1, item2) will complete and be recorded as done
    QTRY_COMPARE(queue.job(jobId).value_or(JobInfo { }).done, 2);
    QCOMPARE(queue.job(jobId).value_or(JobInfo { }).state, JobState::Paused);
    QCOMPARE(handlerPtr->state().processedKeys.size(), 2);

    // Wait a little to ensure no new items are dispatched while paused
    QTest::qWait(50);
    QCOMPARE(queue.job(jobId).value_or(JobInfo { }).done, 2);
    QCOMPARE(handlerPtr->state().processedKeys.size(), 2);

    // Resume the job
    queue.resume(jobId);
    QTRY_COMPARE_WITH_TIMEOUT(
        queue.job(jobId).value_or(JobInfo { }).state, JobState::Completed, 5000);

    const auto finalJob = queue.job(jobId);
    QVERIFY(finalJob.has_value());
    if (!finalJob.has_value()) {
        return;
    }
    QCOMPARE(finalJob->done, 5);
    QCOMPARE(finalJob->failed, 0);
    QCOMPARE(handlerPtr->state().processedKeys, items);
}

void TstJobQueue::resumesAfterRestart()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath(QStringLiteral("test.db"));
    ManualClock clock(1000);
    const QStringList items = {
        QStringLiteral("item1"),
        QStringLiteral("item2"),
        QStringLiteral("item3"),
        QStringLiteral("item4"),
        QStringLiteral("item5"),
    };
    qint64 jobId = 0;

    {
        Database db(dbPath);
        const Migrator migrator;
        QVERIFY(db.open(migrator).ok());

        JobQueue queue1(db, clock);
        auto handler1 = std::make_unique<FakeHandler>(QStringLiteral("test.fake"), 2);
        handler1->state().behaviorMap.insert(
            QStringLiteral("item1"), CallbackBehavior::AsyncSuccess);
        handler1->state().behaviorMap.insert(
            QStringLiteral("item2"), CallbackBehavior::AsyncSuccess);
        handler1->state().behaviorMap.insert(QStringLiteral("item3"), CallbackBehavior::Never);
        handler1->state().behaviorMap.insert(QStringLiteral("item4"), CallbackBehavior::Never);
        handler1->state().behaviorMap.insert(QStringLiteral("item5"), CallbackBehavior::Never);
        queue1.registerHandler(std::move(handler1));

        const auto enqueueRes
            = queue1.enqueue(QStringLiteral("test.fake"), QStringLiteral("Restart Job"), items);
        QVERIFY(enqueueRes.ok());
        jobId = enqueueRes.value();

        // Wait until first 2 items are done
        QTRY_COMPARE(queue1.job(jobId).value_or(JobInfo { }).done, 2);
    }

    // Now start a new JobQueue on the same database
    {
        Database db(dbPath);
        const Migrator migrator;
        QVERIFY(db.open(migrator).ok());

        JobQueue queue2(db, clock);
        QVERIFY(queue2.restore().ok());

        const auto infoOpt = queue2.job(jobId);
        QVERIFY(infoOpt.has_value());
        if (!infoOpt.has_value()) {
            return;
        }
        QCOMPARE(infoOpt->state, JobState::Paused);
        QCOMPARE(infoOpt->done, 2);
        QCOMPARE(infoOpt->total, 5);

        auto handler2 = std::make_unique<FakeHandler>(QStringLiteral("test.fake"), 2);
        auto *handler2Ptr = handler2.get();
        queue2.registerHandler(std::move(handler2));

        queue2.resume(jobId);
        QTRY_COMPARE_WITH_TIMEOUT(
            queue2.job(jobId).value_or(JobInfo { }).state, JobState::Completed, 5000);

        const auto finalInfo = queue2.job(jobId);
        QVERIFY(finalInfo.has_value());
        if (!finalInfo.has_value()) {
            return;
        }
        QCOMPARE(finalInfo->done, 5);
        QCOMPARE(finalInfo->failed, 0);

        // Verify only remaining items were dispatched to handler2
        const QStringList expectedRemaining = {
            QStringLiteral("item3"),
            QStringLiteral("item4"),
            QStringLiteral("item5"),
        };
        QCOMPARE(handler2Ptr->state().processedKeys, expectedRemaining);
    }
}

void TstJobQueue::failuresAndConfigErrors()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    ManualClock clock(1000);
    JobQueue queue(db, clock);

    auto handler = std::make_unique<FakeHandler>(QStringLiteral("test.fake"), 1);
    auto *handlerPtr = handler.get();
    queue.registerHandler(std::move(handler));

    // Part A: kHttp error -> recorded as failed, job completes; retryFailed re-runs it successfully
    handlerPtr->state().behaviorMap.insert(
        QStringLiteral("fail_item"), CallbackBehavior::AsyncError);
    handlerPtr->state().errorMap.insert(QStringLiteral("fail_item"),
        core::Error {
            .code = QString(errc::kHttp),
            .message = QStringLiteral("HTTP 500 Internal Error"),
            .detail = { },
        });
    handlerPtr->state().behaviorMap.insert(
        QStringLiteral("ok_item"), CallbackBehavior::AsyncSuccess);

    const QStringList job1Items = { QStringLiteral("fail_item"), QStringLiteral("ok_item") };
    const auto job1Res
        = queue.enqueue(QStringLiteral("test.fake"), QStringLiteral("Job 1"), job1Items);
    QVERIFY(job1Res.ok());
    const qint64 job1Id = job1Res.value();

    QTRY_COMPARE_WITH_TIMEOUT(
        queue.job(job1Id).value_or(JobInfo { }).state, JobState::Completed, 5000);

    auto job1Info = queue.job(job1Id);
    QVERIFY(job1Info.has_value());
    if (!job1Info.has_value()) {
        return;
    }
    QCOMPARE(job1Info->done, 1);
    QCOMPARE(job1Info->failed, 1);

    // Make fail_item succeed on retry
    handlerPtr->state().behaviorMap.insert(
        QStringLiteral("fail_item"), CallbackBehavior::AsyncSuccess);
    queue.retryFailed(job1Id);

    QTRY_COMPARE_WITH_TIMEOUT(
        queue.job(job1Id).value_or(JobInfo { }).state, JobState::Completed, 5000);
    job1Info = queue.job(job1Id);
    QVERIFY(job1Info.has_value());
    if (!job1Info.has_value()) {
        return;
    }
    QCOMPARE(job1Info->done, 2);
    QCOMPARE(job1Info->failed, 0);

    // Part B: kAuth error -> job paused, last_error contains ai.auth, item remains pending
    handlerPtr->state().behaviorMap.insert(
        QStringLiteral("auth_item"), CallbackBehavior::AsyncError);
    handlerPtr->state().errorMap.insert(QStringLiteral("auth_item"),
        core::Error {
            .code = QString(errc::kAuth),
            .message = QStringLiteral("Invalid API key"),
            .detail = { },
        });

    const QStringList job2Items = { QStringLiteral("auth_item"), QStringLiteral("other_item") };
    const auto job2Res
        = queue.enqueue(QStringLiteral("test.fake"), QStringLiteral("Job 2"), job2Items);
    QVERIFY(job2Res.ok());
    const qint64 job2Id = job2Res.value();

    QTRY_COMPARE_WITH_TIMEOUT(
        queue.job(job2Id).value_or(JobInfo { }).state, JobState::Paused, 5000);

    const auto job2Info = queue.job(job2Id);
    QVERIFY(job2Info.has_value());
    if (!job2Info.has_value()) {
        return;
    }
    QCOMPARE(job2Info->state, JobState::Paused);
    QVERIFY(job2Info->lastError.contains(QString(errc::kAuth)));

    // Verify auth_item remains pending in DB
    const auto connRes = db.connection();
    QVERIFY(connRes.ok());
    QSqlQuery q(connRes.value());
    q.prepare(QStringLiteral("SELECT state FROM job_items WHERE job_id = ? AND item_key = ?"));
    q.addBindValue(job2Id);
    q.addBindValue(QStringLiteral("auth_item"));
    QVERIFY(q.exec() && q.next());
    QCOMPARE(q.value(0).toString(), QStringLiteral("pending"));
}

void TstJobQueue::cancelAbortsInFlight()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    ManualClock clock(1000);
    JobQueue queue(db, clock);

    auto handler = std::make_unique<FakeHandler>(QStringLiteral("test.fake"), 2);
    handler->state().defaultBehavior = CallbackBehavior::Never;
    auto *handlerPtr = handler.get();
    queue.registerHandler(std::move(handler));

    const QStringList items = { QStringLiteral("item1"), QStringLiteral("item2") };
    const auto enqueueRes
        = queue.enqueue(QStringLiteral("test.fake"), QStringLiteral("Cancel Job"), items);
    QVERIFY(enqueueRes.ok());
    const qint64 jobId = enqueueRes.value();

    QCOMPARE(handlerPtr->state().currentInFlight, 2);

    queue.cancel(jobId);

    // Cancel destroys in-flight work objects immediately
    QCOMPARE(handlerPtr->state().currentInFlight, 0);

    const auto jobInfo = queue.job(jobId);
    QVERIFY(jobInfo.has_value());
    if (!jobInfo.has_value()) {
        return;
    }
    QCOMPARE(jobInfo->state, JobState::Cancelled);
    QCOMPARE(jobInfo->done, 0);
    QCOMPARE(jobInfo->failed, 0);

    // Test remove
    queue.remove(jobId);
    QVERIFY(!queue.job(jobId).has_value());
}

void TstJobQueue::estimateSumsAndRoughTokenCount()
{
    QCOMPARE(roughTokenCount(QStringLiteral("abcdefgh")), 2);
    QCOMPARE(roughTokenCount(QStringLiteral("你好")), 2);
    QCOMPARE(roughTokenCount(QStringLiteral("你好ab")), 3);
    QCOMPARE(roughTokenCount(QStringLiteral("")), 0);

    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    Database db(tempDir.filePath(QStringLiteral("test.db")));
    const Migrator migrator;
    QVERIFY(db.open(migrator).ok());

    ManualClock clock(1000);
    JobQueue queue(db, clock);

    // Unregistered kind returns kNotConfigured
    const auto resUnreg
        = queue.estimate(QStringLiteral("unregistered.kind"), { QStringLiteral("hello") });
    QVERIFY(!resUnreg.ok());
    QCOMPARE(resUnreg.error().code, errc::kNotConfigured);

    auto handler = std::make_unique<FakeHandler>(QStringLiteral("test.fake"));
    queue.registerHandler(std::move(handler));

    const QStringList items = { QStringLiteral("你好"), QStringLiteral("abcdefgh") };
    const auto res = queue.estimate(QStringLiteral("test.fake"), items);
    QVERIFY(res.ok());
    // "你好" prompt: 2, completion: 10
    // "abcdefgh" prompt: 2, completion: 10
    // Total: prompt = 4, completion = 20
    QCOMPARE(res.value().promptTokens, 4);
    QCOMPARE(res.value().completionTokens, 20);
}

void TstJobQueue::concurrentWritesDoNotBlockJob()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString dbPath = tempDir.filePath(QStringLiteral("test_concurrent.db"));
    Database db1(dbPath);
    Database db2(dbPath);
    const Migrator migrator;
    QVERIFY(db1.open(migrator).ok());
    QVERIFY(db2.open(migrator).ok());

    ManualClock clock(1000);
    JobQueue queue(db1, clock);

    // JobQueue 在一次读写之间发出 jobChanged；在槽里用另一连接提交写入，
    // 模拟后台扫描恰好在此刻提交（曾导致 database is locked 后任务永久停住）。
    int writes = 0;
    QObject::connect(&queue, &JobQueue::jobChanged, &queue, [&db2, &writes] {
        const auto conn2Res = db2.connection();
        QVERIFY(conn2Res.ok());
        QSqlQuery q2(conn2Res.value());
        q2.prepare(QStringLiteral("INSERT INTO library_roots (path, added_at) VALUES (?, 1000)"));
        q2.addBindValue(QStringLiteral("/root/%1").arg(++writes));
        QVERIFY(q2.exec());
    });

    auto handler = std::make_unique<FakeHandler>(QStringLiteral("test.fake"), 1);
    queue.registerHandler(std::move(handler));

    const QStringList items = {
        QStringLiteral("item1"),
        QStringLiteral("item2"),
        QStringLiteral("item3"),
        QStringLiteral("item4"),
        QStringLiteral("item5"),
    };

    const auto enqueueRes
        = queue.enqueue(QStringLiteral("test.fake"), QStringLiteral("Concurrent Test"), items);
    QVERIFY(enqueueRes.ok());
    const qint64 jobId = enqueueRes.value();

    QTRY_COMPARE_WITH_TIMEOUT(
        queue.job(jobId).value_or(JobInfo { }).state, JobState::Completed, 5000);

    const auto jobOpt = queue.job(jobId);
    QVERIFY(jobOpt.has_value());
    if (!jobOpt.has_value()) {
        return;
    }

    QCOMPARE(jobOpt->state, JobState::Completed);
    QCOMPARE(jobOpt->total, 5);
    QCOMPARE(jobOpt->done, 5);
    QCOMPARE(jobOpt->failed, 0);
}

} // namespace

QTEST_GUILESS_MAIN(TstJobQueue)

#include "tst_JobQueue.moc"
