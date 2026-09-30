// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QByteArray>
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
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace linernotes::butler {

class MusicBrainzClient;

class MbFetch : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MbFetch)

public:
    ~MbFetch() override;
    [[nodiscard]] bool isFinished() const;
    [[nodiscard]] bool fromCache() const;
    [[nodiscard]] const core::Result<QByteArray> &result() const;

signals:
    void finished();

private:
    friend class MusicBrainzClient;

    explicit MbFetch(MusicBrainzClient *client, bool fromCache = false, QObject *parent = nullptr);
    void finish(core::Result<QByteArray> res);

    QPointer<MusicBrainzClient> m_client;
    bool m_finished = false;
    bool m_fromCache = false;
    core::Result<QByteArray> m_result { core::Error { } };
};

class MusicBrainzClient final : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MusicBrainzClient)

public:
    MusicBrainzClient(QNetworkAccessManager &network, library::Database &db,
        const core::Clock &clock, QObject *parent = nullptr);
    ~MusicBrainzClient() override;

    /// 先查 mb_cache（键为完整 URL 字符串，未过期 kMaxCacheAgeMs = 30
    /// 天）：命中则异步（下一轮事件循环）完成，不占限速额度。 否则入队；全局至少间隔 1
    /// 秒发一个请求（MusicBrainz 要求 ≤ 1 req/s），同一时间最多一个请求在途。
    std::unique_ptr<MbFetch> get(const QUrl &url);

    /// 只查缓存，不发请求
    [[nodiscard]] std::optional<QByteArray> cached(const QUrl &url) const;

private:
    friend class MbFetch;

    struct PendingRequest {
        MbFetch *fetch = nullptr;
        QUrl url;
        int retryCount = 0;
    };

    void onFetchDestroyed(MbFetch *fetch);
    void enqueueRequest(MbFetch *fetch, const QUrl &url);
    void processQueue();
    void sendNextRequest();
    void sendNetworkRequest(const QUrl &url);
    void onNetworkReplyFinished();
    void onRetryTimeout();
    void writeCache(const QString &url, const QString &body, qint64 fetchedAt);

    QNetworkAccessManager &m_network;
    library::Database &m_db;
    const core::Clock &m_clock;

    qint64 m_lastSentMs = 0;
    QList<PendingRequest> m_queue;
    QPointer<QNetworkReply> m_inFlightReply;
    MbFetch *m_inFlightFetch = nullptr;
    QUrl m_inFlightUrl;
    int m_inFlightRetryCount = 0;
    QTimer *m_rateLimitTimer = nullptr;
    QTimer *m_retryTimer = nullptr;
};

} // namespace linernotes::butler
