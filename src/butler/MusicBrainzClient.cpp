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

constexpr int kTransferTimeoutMs = 15000; // 15 seconds

} // namespace

MbSearchTask::MbSearchTask(MusicBrainzClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{
}

MbSearchTask::~MbSearchTask()
{
    if (m_client != nullptr) {
        m_client->onTaskDestroyed(this);
    }
}

bool MbSearchTask::isFinished() const
{
    return m_finished;
}

const core::Result<QList<MbArtist>> &MbSearchTask::result() const
{
    Q_ASSERT_X(m_finished, "MbSearchTask::result", "Called result() before task finished");
    return m_result;
}

void MbSearchTask::finish(core::Result<QList<MbArtist>> res)
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
{
    m_rateLimitTimer->setSingleShot(true);
    connect(m_rateLimitTimer, &QTimer::timeout, this, &MusicBrainzClient::sendNextRequest);
}

MusicBrainzClient::~MusicBrainzClient()
{
    if (m_rateLimitTimer != nullptr) {
        m_rateLimitTimer->stop();
    }
    if (m_inFlightReply != nullptr) {
        m_inFlightReply->disconnect(this);
        m_inFlightReply->abort();
        m_inFlightReply->deleteLater();
        m_inFlightReply = nullptr;
    }
    m_queue.clear();
}

std::optional<QList<MbArtist>> MusicBrainzClient::cachedSearch(const QString &name) const
{
    auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return std::nullopt;
    }
    return cachedArtistSearch(connRes.value(), name, m_clock.nowMs());
}

std::unique_ptr<MbSearchTask> MusicBrainzClient::searchArtist(const QString &name)
{
    auto task = std::unique_ptr<MbSearchTask>(new MbSearchTask(this));

    // Check cache
    auto cached = cachedSearch(name);
    if (cached.has_value()) {
        qCDebug(lcButler) << "MusicBrainz cache hit for" << name;
        QTimer::singleShot(
            0, task.get(), [t = task.get(), res = std::move(cached.value())]() mutable {
                t->finish(std::move(res));
            });
        return task;
    }

    const QUrl url = artistSearchUrl(name);
    qCDebug(lcButler) << "MusicBrainz cache miss for" << url.toString();
    enqueueRequest(task.get(), url);
    return task;
}

void MusicBrainzClient::onTaskDestroyed(MbSearchTask *task)
{
    if (m_inFlightTask == task) {
        if (m_inFlightReply != nullptr) {
            m_inFlightReply->disconnect(this);
            m_inFlightReply->abort();
            m_inFlightReply->deleteLater();
            m_inFlightReply = nullptr;
        }
        m_inFlightTask = nullptr;
        m_inFlightUrl = QUrl();
        processQueue();
    }

    for (auto it = m_queue.begin(); it != m_queue.end();) {
        if (it->task == task) {
            it = m_queue.erase(it);
        } else {
            ++it;
        }
    }
}

void MusicBrainzClient::enqueueRequest(MbSearchTask *task, const QUrl &url)
{
    m_queue.append(PendingRequest { .task = task, .url = url });
    processQueue();
}

void MusicBrainzClient::processQueue()
{
    // 队列中的任务都还活着：任务销毁时 onTaskDestroyed 会把它移出队列。
    if (m_inFlightReply != nullptr || m_rateLimitTimer->isActive() || m_queue.isEmpty()) {
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
    if (m_queue.isEmpty() || m_inFlightReply != nullptr) {
        return;
    }

    const PendingRequest req = m_queue.takeFirst();
    m_lastSentMs = m_clock.nowMs();
    m_inFlightTask = req.task;
    m_inFlightUrl = req.url;

    QNetworkRequest netReq(req.url);
    const QString userAgent
        = QStringLiteral("Linernotes/%1.%2.%3 ( https://github.com/liugantang/Linernotes )")
              .arg(core::kVersionMajor)
              .arg(core::kVersionMinor)
              .arg(core::kVersionPatch);
    netReq.setRawHeader("User-Agent", userAgent.toUtf8());
    netReq.setRawHeader("Accept", "application/json");
    netReq.setTransferTimeout(kTransferTimeoutMs);

    QNetworkReply *reply = m_network.get(netReq);
    m_inFlightReply = reply;
    connect(reply, &QNetworkReply::finished, this, &MusicBrainzClient::onNetworkReplyFinished);
}

void MusicBrainzClient::onNetworkReplyFinished()
{
    if (m_inFlightReply == nullptr) {
        return;
    }

    QNetworkReply *reply = m_inFlightReply;
    MbSearchTask *task = m_inFlightTask;
    const QUrl url = m_inFlightUrl;

    m_inFlightReply = nullptr;
    m_inFlightTask = nullptr;
    m_inFlightUrl = QUrl();

    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError netErr = reply->error();
    const QByteArray body = reply->readAll();

    if (task != nullptr) {
        if (status == 503) {
            task->finish(core::Error {
                .code = QString(errc::kMbRateLimited),
                .message = QStringLiteral("MusicBrainz rate limited"),
                .detail = QStringLiteral("HTTP 503"),
            });
        } else if (status != 200 || netErr != QNetworkReply::NoError) {
            QString detail;
            if (status > 0) {
                detail = QStringLiteral("HTTP %1: %2")
                             .arg(QString::number(status), reply->errorString());
            } else {
                detail = reply->errorString();
            }
            task->finish(core::Error {
                .code = QString(errc::kMbNetwork),
                .message = QStringLiteral("MusicBrainz network request failed"),
                .detail = detail,
            });
        } else {
            auto parseRes = parseArtistSearch(body);
            if (!parseRes.ok()) {
                task->finish(parseRes.error());
            } else {
                writeCache(url.toString(), QString::fromUtf8(body), m_clock.nowMs());
                task->finish(std::move(parseRes.value()));
            }
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
