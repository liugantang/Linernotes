// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QFile>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <ai/JobHandler.h>
#include <ai/JobQueue.h>
#include <audio/TrackEmbedding.h>
#include <common/ManualClock.h>
#include <library/Database.h>
#include <library/EmbeddingStore.h>
#include <library/Migrator.h>
#include <ui/AudioAnalysisController.h>

#include <memory>

namespace {

using linernotes::ai::JobHandler;
using linernotes::ai::JobQueue;
using linernotes::ai::TokenUsage;
using linernotes::library::Database;
using linernotes::library::EmbeddingStore;
using linernotes::library::Migrator;
using linernotes::test::ManualClock;
using linernotes::ui::AudioAnalysisController;

QString modelId()
{
    return QString::fromLatin1(
        linernotes::audio::kEmbeddingModelId.data(), linernotes::audio::kEmbeddingModelId.size());
}

// 与真实处理器一样异步完成，只是不跑模型，直接存一个向量。
class SavingHandler final : public JobHandler {
public:
    SavingHandler(Database &db, const ManualClock &clock)
        : m_db(db)
        , m_clock(clock)
    {
    }

    [[nodiscard]] QString kind() const override { return QStringLiteral("audio.embed"); }
    [[nodiscard]] int maxInFlight() const override { return 1; }
    [[nodiscard]] TokenUsage estimate(
        const QString & /*itemKey*/, const QJsonObject & /*params*/) const override
    {
        return { };
    }

    std::unique_ptr<QObject> process(const QString &itemKey, const QJsonObject & /*params*/,
        std::function<void(const linernotes::core::Result<void> &)> done) override
    {
        auto owner = std::make_unique<QObject>();
        QTimer::singleShot(0, owner.get(), [this, itemKey, done = std::move(done)]() {
            EmbeddingStore store(m_db, m_clock);
            done(store.save(itemKey.toLongLong(), modelId(), QList<float>(1024, 0.5F)));
        });
        return owner;
    }

private:
    Database &m_db;
    const ManualClock &m_clock;
};

class TstAudioAnalysisController : public QObject {
    Q_OBJECT

private slots:
    void countsFollowProgress();
};

void TstAudioAnalysisController::countsFollowProgress()
{
    const QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    Database db(tempDir.filePath(QStringLiteral("test.db")));
    QVERIFY(db.open(Migrator()).ok());
    const auto conn = db.connection().value();

    QSqlQuery q(conn);
    QVERIFY(q.exec(
        QStringLiteral("INSERT INTO library_roots (path, added_at) VALUES ('/music', 1000);")));
    for (int i = 1; i <= 3; ++i) {
        QVERIFY(
            q.exec(QStringLiteral("INSERT INTO files (root_id, path, size, mtime, content_hash, "
                                  "duration_ms, first_seen_at, scanned_at) "
                                  "VALUES (1, '/music/%1.flac', 1024, 2000, 'h%1', 60000, "
                                  "2000, 2000);")
                    .arg(i)));
        QVERIFY(q.exec(QStringLiteral("INSERT INTO tracks (file_id, tags_read_at, created_at) "
                                      "VALUES (%1, 1000, 3000);")
                .arg(i)));
    }

    // 模型文件只需要存在。
    const QString modelPath = tempDir.filePath(QStringLiteral("model.onnx"));
    QFile modelFile(modelPath);
    QVERIFY(modelFile.open(QIODevice::WriteOnly));
    modelFile.close();

    ManualClock clock;
    JobQueue jobs(db, clock);
    jobs.registerHandler(std::make_unique<SavingHandler>(db, clock));
    AudioAnalysisController controller(db, clock, jobs, modelPath);
    QCOMPARE(controller.analyzedCount(), 0);
    QCOMPARE(controller.pendingCount(), 3);

    controller.start();
    QTRY_COMPARE(controller.done(), 3);
    QTRY_COMPARE(controller.analyzedCount(), 3);
    QCOMPARE(controller.pendingCount(), 0);
}

} // namespace

QTEST_GUILESS_MAIN(TstAudioAnalysisController)
#include "tst_AudioAnalysisController.moc"
