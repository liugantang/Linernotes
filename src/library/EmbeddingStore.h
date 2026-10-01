// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

#include <core/Result.h>

namespace linernotes::core {
class Clock;
}

namespace linernotes::library {

class Database;

struct EmbedSource {
    qint64 trackId = 0;
    QString path;
    qint64 startMs = 0;
    qint64 durationMs = 0;
    bool operator==(const EmbedSource &) const = default;
};

class EmbeddingStore {
public:
    EmbeddingStore(Database &db, const core::Clock &clock);

    // 需要计算的曲目：文件未缺失、无 scan_error，且 audio_embeddings 无记录、model 不同或
    // content_hash 不同（NULL 视为不同）。按 track_id 升序。
    [[nodiscard]] core::Result<QList<qint64>> pendingTrackIds(const QString &model) const;

    // 路径与时长：CUE 分轨用 tracks.start_ms / end_ms（end_ms 为空时用 files.duration_ms -
    // start_ms），普通文件 start 0、files.duration_ms
    [[nodiscard]] core::Result<EmbedSource> source(qint64 trackId) const;

    core::Result<void> save(qint64 trackId, const QString &model,
        const QList<float> &vector); // 存 float16（qfloat16），写当前 content_hash
    core::Result<void> saveFailure(
        qint64 trackId, const QString &model, const QString &error); // 失败也记，避免反复重试

    // 载入当前 model 下成功且未过期的全部向量（转回 float）；library 模块不依赖 audio
    // 模块，由调用方 add 进 EmbeddingIndex
    struct StoredEmbedding {
        qint64 trackId = 0;
        QList<float> vector;
        bool operator==(const StoredEmbedding &) const = default;
    };
    [[nodiscard]] core::Result<QList<StoredEmbedding>> loadAll(const QString &model) const;

    // 当前 model 下成功且未过期的曲目数
    [[nodiscard]] core::Result<int> analyzedCount(const QString &model) const;

private:
    Database &m_db;
    const core::Clock &m_clock;
};

} // namespace linernotes::library
