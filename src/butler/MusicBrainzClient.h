// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <butler/MusicBrainz.h>
#include <core/Clock.h>
#include <core/Result.h>
#include <library/Database.h>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace linernotes::butler {

class MusicBrainzClient;

class MbSearchTask : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MbSearchTask)

public:
    ~MbSearchTask() override;
    [[nodiscard]] bool isFinished() const;
    [[nodiscard]] const core::Result<QList<MbArtist>> &result() const;

signals:
    void finished();

private:
    friend class MusicBrainzClient;

    explicit MbSearchTask(MusicBrainzClient *client, QObject *parent = nullptr);
    void finish(core::Result<QList<MbArtist>> res);

    QPointer<MusicBrainzClient> m_client;
    bool m_finished = false;
    core::Result<QList<MbArtist>> m_result { core::Error { } };
};

class MusicBrainzClient final : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MusicBrainzClient)

public:
    /// 依赖由调用方持有，生命周期长于客户端。
    MusicBrainzClient(QNetworkAccessManager &network, library::Database &db,
        const core::Clock &clock, QObject *parent = nullptr);
    ~MusicBrainzClient() override;

    /// 只查 mb_cache，不发请求。未缓存、已过期（kMaxCacheAgeMs）或解析失败 → nullopt。
    [[nodiscard]] std::optional<QList<MbArtist>> cachedSearch(const QString &name) const;

    std::unique_ptr<MbSearchTask> searchArtist(const QString &name);

private:
    friend class MbSearchTask;

    struct PendingRequest {
        MbSearchTask *task = nullptr;
        QUrl url;
    };

    void onTaskDestroyed(MbSearchTask *task);
    void enqueueRequest(MbSearchTask *task, const QUrl &url);
    void processQueue();
    void sendNextRequest();
    void onNetworkReplyFinished();
    void writeCache(const QString &url, const QString &body, qint64 fetchedAt);

    QNetworkAccessManager &m_network;
    library::Database &m_db;
    const core::Clock &m_clock;

    qint64 m_lastSentMs = 0;
    QList<PendingRequest> m_queue;
    QPointer<QNetworkReply> m_inFlightReply;
    MbSearchTask *m_inFlightTask = nullptr;
    QUrl m_inFlightUrl;
    QTimer *m_rateLimitTimer = nullptr;
};

} // namespace linernotes::butler
