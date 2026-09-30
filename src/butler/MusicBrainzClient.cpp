// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "MusicBrainzClient.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>

#include <butler/ButlerLogging.h>
#include <butler/Errors.h>
#include <core/Version.h>

#include <algorithm>
#include <utility>

namespace linernotes::butler {

namespace {

constexpr int kTransferTimeoutMs = 30000; // 30 seconds
constexpr int kMaxRetries = 3;

int retryDelayMs(int retryCount)
{
    // retryCount: 0 -> 2000ms (2s), 1 -> 4000ms (4s), 2 -> 8000ms (8s)
    return 2000 * (1 << std::min(retryCount, 30));
}

} // namespace

MbFetch::MbFetch(MusicBrainzClient *client, bool fromCache, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_fromCache(fromCache)
{
}

MbFetch::~MbFetch()
{
    if (m_client != nullptr) {
        m_client->onFetchDestroyed(this);
    }
}

bool MbFetch::isFinished() const
{
    return m_finished;
}

bool MbFetch::fromCache() const
{
    return m_fromCache;
}

const core::Result<QByteArray> &MbFetch::result() const
{
    return m_result;
}

void MbFetch::finish(core::Result<QByteArray> res)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    m_result = std::move(res);
    emit finished();
}

MusicBrainzClient::MusicBrainzClient(QNetworkAccessManager &network, library::Database &db,
    const core::Clock &clock, QObject *parent)
    : QObject(parent)
    , m_network(network)
    , m_db(db)
    , m_clock(clock)
    , m_rateLimitTimer(new QTimer(this))
    , m_retryTimer(new QTimer(this))
{
    m_rateLimitTimer->setSingleShot(true);
    connect(m_rateLimitTimer, &QTimer::timeout, this, &MusicBrainzClient::sendNextRequest);

    m_retryTimer->setSingleShot(true);
    connect(m_retryTimer, &QTimer::timeout, this, &MusicBrainzClient::onRetryTimeout);
}

MusicBrainzClient::~MusicBrainzClient()
{
    if (m_rateLimitTimer != nullptr) {
        m_rateLimitTimer->stop();
    }
    if (m_retryTimer != nullptr) {
        m_retryTimer->stop();
    }
    if (m_inFlightReply != nullptr) {
        m_inFlightReply->disconnect(this);
        m_inFlightReply->abort();
        m_inFlightReply->deleteLater();
        m_inFlightReply = nullptr;
    }
    m_queue.clear();
}

std::optional<QByteArray> MusicBrainzClient::cached(const QUrl &url) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return std::nullopt;
    }
    const QString urlString = url.toString();
    QSqlQuery q(connRes.value());
    q.prepare(QStringLiteral("SELECT body, fetched_at FROM mb_cache WHERE url = ?;"));
    q.addBindValue(urlString);
    if (q.exec() && q.next()) {
        const QString body = q.value(0).toString();
        const qint64 fetchedAt = q.value(1).toLongLong();
        const qint64 now = m_clock.nowMs();
        if (now >= fetchedAt && (now - fetchedAt) < kMaxCacheAgeMs) {
            return body.toUtf8();
        }
    }
    return std::nullopt;
}

std::unique_ptr<MbFetch> MusicBrainzClient::get(const QUrl &url)
{
    auto cachedBody = cached(url);
    if (cachedBody.has_value()) {
        qCDebug(lcButler) << "MusicBrainz cache hit for" << url.toString();
        auto fetch = std::unique_ptr<MbFetch>(new MbFetch(this, true));
        QTimer::singleShot(
            0, fetch.get(), [f = fetch.get(), body = std::move(cachedBody.value())]() mutable {
                f->finish(core::Result<QByteArray>(std::move(body)));
            });
        return fetch;
    }

    qCDebug(lcButler) << "MusicBrainz cache miss for" << url.toString();
    auto fetch = std::unique_ptr<MbFetch>(new MbFetch(this, false));
    enqueueRequest(fetch.get(), url);
    return fetch;
}

void MusicBrainzClient::onFetchDestroyed(MbFetch *fetch)
{
    if (m_inFlightFetch == fetch) {
        if (m_inFlightReply != nullptr) {
            m_inFlightReply->disconnect(this);
            m_inFlightReply->abort();
            m_inFlightReply->deleteLater();
            m_inFlightReply = nullptr;
        }
        if (m_retryTimer != nullptr && m_retryTimer->isActive()) {
            m_retryTimer->stop();
        }
        m_inFlightFetch = nullptr;
        m_inFlightUrl = QUrl();
        m_inFlightRetryCount = 0;
        processQueue();
    }

    for (auto it = m_queue.begin(); it != m_queue.end();) {
        if (it->fetch == fetch) {
            it = m_queue.erase(it);
        } else {
            ++it;
        }
    }
}

void MusicBrainzClient::enqueueRequest(MbFetch *fetch, const QUrl &url)
{
    m_queue.append(PendingRequest { .fetch = fetch, .url = url, .retryCount = 0 });
    processQueue();
}

void MusicBrainzClient::processQueue()
{
    if (m_inFlightReply != nullptr || m_inFlightFetch != nullptr || m_rateLimitTimer->isActive()
        || m_queue.isEmpty()) {
        return;
    }

    const qint64 elapsed = m_clock.nowMs() - m_lastSentMs;
    const qint64 delay = (m_lastSentMs == 0) ? 0 : std::max<qint64>(0, 1000 - elapsed);
    if (delay > 0) {
        m_rateLimitTimer->start(static_cast<int>(delay));
        return;
    }
    sendNextRequest();
}

void MusicBrainzClient::sendNextRequest()
{
    if (m_queue.isEmpty() || m_inFlightReply != nullptr || m_inFlightFetch != nullptr) {
        return;
    }

    const PendingRequest req = m_queue.takeFirst();
    m_inFlightFetch = req.fetch;
    m_inFlightUrl = req.url;
    m_inFlightRetryCount = req.retryCount;

    sendNetworkRequest(m_inFlightUrl);
}

void MusicBrainzClient::sendNetworkRequest(const QUrl &url)
{
    m_lastSentMs = m_clock.nowMs();

    QNetworkRequest netReq(url);
    const QString userAgent
        = QStringLiteral("Linernotes/%1 ( https://github.com/linernotes/linernotes )")
              .arg(core::versionString());
    netReq.setRawHeader("User-Agent", userAgent.toUtf8());
    netReq.setRawHeader("Accept", "application/json");
    netReq.setTransferTimeout(kTransferTimeoutMs);

    QNetworkReply *reply = m_network.get(netReq);
    m_inFlightReply = reply;
    connect(reply, &QNetworkReply::finished, this, &MusicBrainzClient::onNetworkReplyFinished);
}

void MusicBrainzClient::onRetryTimeout()
{
    if (m_inFlightFetch == nullptr) {
        processQueue();
        return;
    }
    sendNetworkRequest(m_inFlightUrl);
}

void MusicBrainzClient::onNetworkReplyFinished()
{
    if (m_inFlightReply == nullptr) {
        return;
    }

    QNetworkReply *reply = m_inFlightReply;
    MbFetch *fetch = m_inFlightFetch;
    const QUrl url = m_inFlightUrl;
    const int retryCount = m_inFlightRetryCount;

    m_inFlightReply = nullptr;
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError netErr = reply->error();
    const QByteArray body = reply->readAll();

    if (status == 503 || status == 429) {
        if (retryCount < kMaxRetries) {
            const int delay = retryDelayMs(retryCount);
            m_inFlightRetryCount = retryCount + 1;
            qCWarning(lcButler) << "MusicBrainz rate limited (HTTP" << status << "), retrying in"
                                << delay << "ms (attempt" << m_inFlightRetryCount << "of"
                                << kMaxRetries << ")";
            m_retryTimer->start(delay);
            return;
        }

        m_inFlightFetch = nullptr;
        m_inFlightUrl = QUrl();
        m_inFlightRetryCount = 0;
        if (fetch != nullptr) {
            fetch->finish(core::Error {
                .code = QString(errc::kMbRateLimited),
                .message = QStringLiteral("MusicBrainz rate limited"),
                .detail = QStringLiteral("HTTP %1 after %2 retries: %3")
                    .arg(QString::number(status), QString::number(retryCount), url.toString()),
            });
        }
        processQueue();
        return;
    }

    m_inFlightFetch = nullptr;
    m_inFlightUrl = QUrl();
    m_inFlightRetryCount = 0;

    if (fetch != nullptr) {
        if (status == 404) {
            fetch->finish(core::Error {
                .code = QString(errc::kMbNotFound),
                .message = QStringLiteral("MusicBrainz entity not found (HTTP 404)"),
                .detail = url.toString(),
            });
        } else if (status != 200 || netErr != QNetworkReply::NoError) {
            QString detail;
            if (status > 0) {
                detail = QStringLiteral("HTTP %1: %2 (%3)")
                             .arg(QString::number(status), reply->errorString(), url.toString());
            } else {
                detail = QStringLiteral("%1 (%2)").arg(reply->errorString(), url.toString());
            }
            fetch->finish(core::Error {
                .code = QString(errc::kMbNetwork),
                .message = QStringLiteral("MusicBrainz network request failed"),
                .detail = detail,
            });
        } else {
            writeCache(url.toString(), QString::fromUtf8(body), m_clock.nowMs());
            fetch->finish(core::Result<QByteArray>(body));
        }
    }

    processQueue();
}

void MusicBrainzClient::writeCache(const QString &url, const QString &body, qint64 fetchedAt)
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return;
    }
    QSqlQuery q(connRes.value());
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO mb_cache (url, body, fetched_at) VALUES (?, ?, ?);"));
    q.addBindValue(url);
    q.addBindValue(body);
    q.addBindValue(fetchedAt);
    if (!q.exec()) {
        qCWarning(lcButler) << "Failed to write mb_cache:" << q.lastError().text();
    }
}

} // namespace linernotes::butler
