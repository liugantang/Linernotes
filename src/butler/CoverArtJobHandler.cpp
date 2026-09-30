// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "CoverArtJobHandler.h"

#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>

#include <butler/ButlerLogging.h>
#include <butler/CoverArtSource.h>
#include <butler/Errors.h>
#include <butler/MusicBrainz.h>
#include <library/Database.h>

#include <memory>
#include <utility>

namespace linernotes::butler {

namespace {

QUrl buildCoverUrl(const QUrl &baseUrl, const QString &type, const QString &id)
{
    QUrl url = baseUrl;
    QString path = url.path();
    if (!path.endsWith(u'/')) {
        path += u'/';
    }
    path += type;
    path += u'/';
    path += id;
    path += QStringLiteral("/front-500");
    url.setPath(path);
    return url;
}

class CoverArtTask final : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(CoverArtTask)

public:
    CoverArtTask(library::Database &db, QNetworkAccessManager &network, library::CoverStore &covers,
        const core::Clock &clock, QUrl baseUrl, CoverArtTarget target,
        std::function<void(const core::Result<void> &)> done)
        : m_db(db)
        , m_network(network)
        , m_covers(covers)
        , m_clock(clock)
        , m_baseUrl(std::move(baseUrl))
        , m_target(std::move(target))
        , m_done(std::move(done))
    {
    }

    ~CoverArtTask() override
    {
        if (m_reply != nullptr) {
            m_reply->disconnect(this);
            m_reply->abort();
            m_reply->deleteLater();
            m_reply = nullptr;
        }
    }

    void start()
    {
        m_tryingReleaseGroup = false;
        m_currentUrl = buildCoverUrl(m_baseUrl, QStringLiteral("release"), m_target.releaseId);
        sendRequest();
    }

private:
    void finish(const core::Result<void> &res)
    {
        if (m_finished) {
            return;
        }
        m_finished = true;
        if (m_reply != nullptr) {
            m_reply->disconnect(this);
            m_reply->deleteLater();
            m_reply = nullptr;
        }
        if (m_done) {
            m_done(res);
        }
    }

    void sendRequest()
    {
        QNetworkRequest req(m_currentUrl);
        req.setAttribute(
            QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setRawHeader("User-Agent", userAgent().toUtf8());
        req.setTransferTimeout(30000);

        m_reply = m_network.get(req);
        connect(m_reply, &QNetworkReply::finished, this, &CoverArtTask::onReplyFinished);
    }

    void onReplyFinished()
    {
        if (m_finished || m_reply == nullptr) {
            return;
        }

        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();

        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto netErr = reply->error();
        const QByteArray body = reply->readAll();

        const bool isNotFound
            = (statusCode == 404) || (netErr == QNetworkReply::ContentNotFoundError);
        if (isNotFound) {
            handleNotFound();
            return;
        }

        const bool isSuccess = (statusCode == 200)
            || (statusCode == 0 && netErr == QNetworkReply::NoError && !body.isEmpty());
        if (isSuccess) {
            handleSuccess(body);
            return;
        }

        QString detail;
        if (statusCode > 0) {
            detail = QStringLiteral("HTTP %1: %2 (%3)")
                         .arg(QString::number(statusCode), reply->errorString(),
                             m_currentUrl.toString());
        } else {
            detail = QStringLiteral("%1 (%2)").arg(reply->errorString(), m_currentUrl.toString());
        }

        finish(core::Error {
            .code = QString(errc::kMbNetwork),
            .message = QStringLiteral("Cover art network request failed"),
            .detail = detail,
        });
    }

    void handleNotFound()
    {
        if (!m_tryingReleaseGroup && !m_target.releaseGroupId.isEmpty()) {
            m_tryingReleaseGroup = true;
            m_currentUrl = buildCoverUrl(
                m_baseUrl, QStringLiteral("release-group"), m_target.releaseGroupId);
            sendRequest();
            return;
        }

        const CoverArtSource source(m_db);
        const auto markRes = source.markChecked(m_target.albumId, m_clock.nowMs());
        if (!markRes.ok()) {
            finish(markRes.error());
            return;
        }
        qCInfo(lcButler, "Cover art album %lld: result=none", m_target.albumId);
        finish({ });
    }

    void handleSuccess(const QByteArray &body)
    {
        const auto ingestRes = m_covers.ingest(body);
        if (!ingestRes.ok()) {
            qCWarning(lcButler, "Failed to decode cover art for album %lld: %s", m_target.albumId,
                qPrintable(ingestRes.error().toString()));
            const CoverArtSource source(m_db);
            const auto markRes = source.markChecked(m_target.albumId, m_clock.nowMs());
            if (!markRes.ok()) {
                finish(markRes.error());
                return;
            }
            qCInfo(lcButler, "Cover art album %lld: result=none", m_target.albumId);
            finish({ });
            return;
        }

        const CoverArtSource source(m_db);
        const auto saveRes
            = source.saveCover(m_target.albumId, ingestRes.value(), m_currentUrl, m_clock.nowMs());
        if (!saveRes.ok()) {
            finish(saveRes.error());
            return;
        }

        qCInfo(lcButler, "Cover art album %lld: result=%s", m_target.albumId,
            m_tryingReleaseGroup ? "release-group" : "release");
        finish({ });
    }

    library::Database &m_db;
    QNetworkAccessManager &m_network;
    library::CoverStore &m_covers;
    const core::Clock &m_clock;
    QUrl m_baseUrl;
    CoverArtTarget m_target;
    std::function<void(const core::Result<void> &)> m_done;

    QUrl m_currentUrl;
    QNetworkReply *m_reply = nullptr;
    bool m_tryingReleaseGroup = false;
    bool m_finished = false;
};

} // namespace

CoverArtJobHandler::CoverArtJobHandler(library::Database &db, QNetworkAccessManager &network,
    library::CoverStore &covers, const core::Clock &clock, QUrl baseUrl)
    : m_db(db)
    , m_network(network)
    , m_covers(covers)
    , m_clock(clock)
    , m_baseUrl(std::move(baseUrl))
{
}

QString CoverArtJobHandler::kind() const
{
    return QStringLiteral("butler.cover_art");
}

int CoverArtJobHandler::maxInFlight() const
{
    return 1;
}

ai::TokenUsage CoverArtJobHandler::estimate(const QString &itemKey, const QJsonObject &params) const
{
    Q_UNUSED(itemKey);
    Q_UNUSED(params);
    return ai::TokenUsage { .promptTokens = 0, .completionTokens = 0 };
}

std::unique_ptr<QObject> CoverArtJobHandler::process(const QString &itemKey,
    const QJsonObject &params, std::function<void(const core::Result<void> &)> done)
{
    Q_UNUSED(params);
    bool ok = false;
    const qint64 albumId = itemKey.toLongLong(&ok);
    if (!ok || albumId <= 0) {
        done(core::Error {
            .code = QString(errc::kMbMatchInvalidKey),
            .message = QStringLiteral("Invalid albumId in itemKey"),
            .detail = itemKey,
        });
        return nullptr;
    }

    const CoverArtSource source(m_db);
    const auto loadRes = source.load(albumId);
    if (!loadRes.ok()) {
        done(loadRes.error());
        return nullptr;
    }

    if (!loadRes.value().has_value()) {
        done({ });
        return nullptr;
    }

    auto task = std::make_unique<CoverArtTask>(
        m_db, m_network, m_covers, m_clock, m_baseUrl, *loadRes.value(), std::move(done));
    task->start();
    return task;
}

} // namespace linernotes::butler

#include "CoverArtJobHandler.moc"
