// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QThreadPool>

#include <ai/JobHandler.h>
#include <core/Result.h>

#include <functional>
#include <memory>

namespace linernotes::core {
class Clock;
}

namespace linernotes::library {
class Database;
}

namespace linernotes::audio {
class AudioEmbedder;
}

namespace linernotes::butler {

class EmbeddingJobHandler final : public ai::JobHandler {
public:
    EmbeddingJobHandler(library::Database &db, const core::Clock &clock, QString modelPath);
    ~EmbeddingJobHandler() override;
    Q_DISABLE_COPY_MOVE(EmbeddingJobHandler)

    [[nodiscard]] QString kind() const override; // "audio.embed"
    [[nodiscard]] int maxInFlight() const override;
    [[nodiscard]] ai::TokenUsage estimate(
        const QString &itemKey, const QJsonObject &params) const override;
    std::unique_ptr<QObject> process(const QString &itemKey, const QJsonObject &params,
        std::function<void(const core::Result<void> &)> done) override;

private:
    library::Database &m_db;
    const core::Clock &m_clock;
    QString m_modelPath;
    QThreadPool m_pool;

    // Thread safety constraint: Lazy loading (AudioEmbedder::create) and usage (embedTrack)
    // both execute exclusively on the worker thread of m_pool. Because m_pool has maxThreadCount ==
    // 1, at most one task runs at any time, so no additional mutex/locking is required for
    // m_embedder.
    std::unique_ptr<audio::AudioEmbedder> m_embedder;
};

} // namespace linernotes::butler
