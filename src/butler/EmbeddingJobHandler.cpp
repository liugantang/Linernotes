// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "EmbeddingJobHandler.h"

#include <QFutureWatcher>
#include <QThread>
#include <QtConcurrent>

#include <audio/AudioEmbedder.h>
#include <audio/TrackEmbedding.h>
#include <butler/Errors.h>
#include <core/Clock.h>
#include <library/Database.h>
#include <library/EmbeddingStore.h>

#include <cstdint>
#include <utility>

namespace linernotes::butler {

namespace {

struct ComputeResult {
    enum class Status : std::uint8_t {
        Success,
        CalculationFailed,
        ModelLoadFailed,
    };
    Status status = Status::Success;
    QList<float> vector;
    core::Error error;
};

class EmbeddingJobWorker final : public QObject {
    Q_OBJECT
public:
    EmbeddingJobWorker(library::Database &db, const core::Clock &clock, qint64 trackId,
        const QFuture<ComputeResult> &future, std::function<void(const core::Result<void> &)> done)
        : m_db(db)
        , m_clock(clock)
        , m_trackId(trackId)
        , m_done(std::move(done))
    {
        connect(&m_watcher, &QFutureWatcher<ComputeResult>::finished, this,
            &EmbeddingJobWorker::onFinished);
        m_watcher.setFuture(future);
    }

    ~EmbeddingJobWorker() override
    {
        m_done = nullptr;
        m_watcher.disconnect(this);
    }

    Q_DISABLE_COPY_MOVE(EmbeddingJobWorker)

private slots:
    void onFinished()
    {
        if (!m_done) {
            return;
        }
        const auto computeRes = m_watcher.result();
        library::EmbeddingStore store(m_db, m_clock);
        const QString modelId
            = QString::fromLatin1(audio::kEmbeddingModelId.data(), audio::kEmbeddingModelId.size());

        if (computeRes.status == ComputeResult::Status::ModelLoadFailed) {
            auto done = std::move(m_done);
            m_done = nullptr;
            done(computeRes.error);
            return;
        }

        if (computeRes.status == ComputeResult::Status::Success) {
            const auto saveRes = store.save(m_trackId, modelId, computeRes.vector);
            if (!saveRes.ok()) {
                auto done = std::move(m_done);
                m_done = nullptr;
                done(saveRes);
                return;
            }
        } else {
            const auto saveRes = store.saveFailure(m_trackId, modelId, computeRes.error.message);
            if (!saveRes.ok()) {
                auto done = std::move(m_done);
                m_done = nullptr;
                done(saveRes);
                return;
            }
        }

        auto done = std::move(m_done);
        m_done = nullptr;
        done({ });
    }

private:
    library::Database &m_db;
    const core::Clock &m_clock;
    qint64 m_trackId;
    std::function<void(const core::Result<void> &)> m_done;
    QFutureWatcher<ComputeResult> m_watcher;
};

} // namespace

EmbeddingJobHandler::EmbeddingJobHandler(
    library::Database &db, const core::Clock &clock, QString modelPath)
    : m_db(db)
    , m_clock(clock)
    , m_modelPath(std::move(modelPath))
{
    m_pool.setMaxThreadCount(1);
    m_pool.setThreadPriority(QThread::LowPriority);
}

EmbeddingJobHandler::~EmbeddingJobHandler()
{
    m_pool.waitForDone();
}

QString EmbeddingJobHandler::kind() const
{
    return QStringLiteral("audio.embed");
}

int EmbeddingJobHandler::maxInFlight() const
{
    return 1;
}

ai::TokenUsage EmbeddingJobHandler::estimate(
    const QString & /*itemKey*/, const QJsonObject & /*params*/) const
{
    return ai::TokenUsage {
        .promptTokens = 0,
        .completionTokens = 0,
    };
}

std::unique_ptr<QObject> EmbeddingJobHandler::process(const QString &itemKey,
    const QJsonObject & /*params*/, std::function<void(const core::Result<void> &)> done)
{
    bool ok = false;
    const qint64 trackId = itemKey.toLongLong(&ok);
    if (!ok || trackId <= 0) {
        done(core::Error {
            .code = QString(errc::kEmbeddingInvalidKey),
            .message = QStringLiteral("Invalid trackId in itemKey"),
            .detail = itemKey,
        });
        return nullptr;
    }

    const library::EmbeddingStore store(m_db, m_clock);
    const auto sourceRes = store.source(trackId);
    if (!sourceRes.ok()) {
        done(sourceRes.error());
        return nullptr;
    }

    const auto &source = sourceRes.value();

    auto future = QtConcurrent::run(&m_pool, [this, source]() -> ComputeResult {
        // 懒加载模型。m_embedder 只在池线程中创建和使用；池最大线程数为 1（构造函数中设定），
        // 所以无需加锁。改动线程数前必须先给 m_embedder 加保护。
        if (m_embedder == nullptr) {
            auto embedderRes = audio::AudioEmbedder::create(m_modelPath, { .intraOpThreads = 2 });
            if (!embedderRes.ok()) {
                return ComputeResult {
                    .status = ComputeResult::Status::ModelLoadFailed,
                    .vector = { },
                    .error = embedderRes.error(),
                };
            }
            m_embedder = std::move(embedderRes.value());
        }

        const auto embedRes
            = audio::embedTrack(*m_embedder, source.path, source.startMs, source.durationMs);
        if (!embedRes.ok()) {
            return ComputeResult {
                .status = ComputeResult::Status::CalculationFailed,
                .vector = { },
                .error = embedRes.error(),
            };
        }

        return ComputeResult {
            .status = ComputeResult::Status::Success,
            .vector = embedRes.value(),
            .error = { },
        };
    });

    return std::make_unique<EmbeddingJobWorker>(m_db, m_clock, trackId, future, std::move(done));
}

} // namespace linernotes::butler

#include "EmbeddingJobHandler.moc"
