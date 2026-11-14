// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "LyricsService.h"

#include "GuideLogging.h"
#include "LocalLyrics.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <core/Version.h>

namespace linernotes::guide {

LyricsService::LyricsService(library::Database &db, QNetworkAccessManager &network,
    const core::Clock &clock, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_network(network)
    , m_clock(clock)
    , m_store(db, clock)
    , m_query(db.connection().ok() ? db.connection().value() : QSqlDatabase { })
{
}

LyricsService::~LyricsService()
{
    for (auto *reply : m_activeRequests) {
        if (reply != nullptr) {
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
        }
    }
    m_activeRequests.clear();
}

std::optional<LyricsResult> LyricsService::localResult(
    qint64 trackId, const library::TrackRow &track, const std::optional<StoredLyrics> &stored) const
{
    // 1. Manual in store
    if (stored.has_value() && stored->kind == StoredLyrics::Kind::Manual) {
        return LyricsResult {
            .trackId = trackId,
            .status = LyricsStatus::Found,
            .lyrics = Lyrics {
                .text = stored->text,
                .source = LyricsSource::Manual,
                .synced = stored->synced,
            },
        };
    }

    // 2. Embedded in raw_tags
    const auto embeddedRes = embeddedLyrics(m_db, trackId);
    if (embeddedRes.ok() && embeddedRes.value().has_value()) {
        const QString &emb = embeddedRes.value().value();
        if (isUsableLyrics(emb)) {
            return LyricsResult {
                .trackId = trackId,
                .status = LyricsStatus::Found,
                .lyrics = Lyrics {
                    .text = emb,
                    .source = LyricsSource::Embedded,
                    .synced = looksSynced(emb),
                },
            };
        }
    }

    // 3. Sidecar .lrc file
    const auto cueRes = isCueTrack(m_db, trackId);
    const bool isCue = cueRes.ok() && cueRes.value();
    if (!isCue) {
        const auto sidecar = sidecarLyrics(track.path);
        if (sidecar.has_value() && isUsableLyrics(sidecar.value())) {
            return LyricsResult {
                .trackId = trackId,
                .status = LyricsStatus::Found,
                .lyrics = Lyrics {
                    .text = sidecar.value(),
                    .source = LyricsSource::LrcFile,
                    .synced = looksSynced(sidecar.value()),
                },
            };
        }
    }

    // 4. Version type is Instrumental
    if (track.versionType.has_value()
        && track.versionType.value() == library::VersionType::Instrumental) {
        return LyricsResult {
            .trackId = trackId,
            .status = LyricsStatus::Instrumental,
            .lyrics = Lyrics { },
        };
    }

    return std::nullopt;
}

std::optional<LyricsResult> LyricsService::cachedResult(
    qint64 trackId, const std::optional<StoredLyrics> &stored) const
{
    if (!stored.has_value()) {
        return std::nullopt;
    }

    // 5. Store lrclib row
    if (stored->kind == StoredLyrics::Kind::Lrclib) {
        if (stored->instrumental) {
            return LyricsResult {
                .trackId = trackId,
                .status = LyricsStatus::Instrumental,
                .lyrics = Lyrics { },
            };
        }
        return LyricsResult {
            .trackId = trackId,
            .status = LyricsStatus::Found,
            .lyrics = Lyrics {
                .text = stored->text,
                .source = LyricsSource::Lrclib,
                .synced = stored->synced,
            },
        };
    }

    // 6. Store none row within 30 days
    if (stored->kind == StoredLyrics::Kind::NotFound) {
        constexpr qint64 kThirtyDaysMs = 30LL * 24 * 60 * 60 * 1000;
        if (m_clock.nowMs() - stored->fetchedAtMs < kThirtyDaysMs) {
            return LyricsResult {
                .trackId = trackId,
                .status = LyricsStatus::NotFound,
                .lyrics = Lyrics { },
            };
        }
    }

    return std::nullopt;
}

void LyricsService::request(qint64 trackId)
{
    const auto tracksRes = m_query.tracksByIds({ trackId });
    if (!tracksRes.ok() || tracksRes.value().isEmpty()) {
        emit finished(LyricsResult {
            .trackId = trackId,
            .status = LyricsStatus::NotFound,
            .lyrics = Lyrics { },
        });
        return;
    }
    const auto &track = tracksRes.value().at(0);

    const auto storedRes = m_store.get(trackId);
    const auto stored = storedRes.ok() ? storedRes.value() : std::nullopt;

    if (const auto local = localResult(trackId, track, stored); local.has_value()) {
        emit finished(local.value());
        return;
    }

    if (const auto cached = cachedResult(trackId, stored); cached.has_value()) {
        emit finished(cached.value());
        return;
    }

    // 7. Online fetch
    fetchOnline(trackId, track);
}

void LyricsService::fetchOnline(qint64 trackId, const library::TrackRow &track)
{
    const auto failIt = m_failedTimestamps.constFind(trackId);
    if (failIt != m_failedTimestamps.constEnd()) {
        constexpr qint64 kTenMinutesMs = 10LL * 60 * 1000;
        if (m_clock.nowMs() - failIt.value() < kTenMinutesMs) {
            emit finished(LyricsResult {
                .trackId = trackId,
                .status = LyricsStatus::Failed,
                .lyrics = Lyrics { },
            });
            return;
        }
    }

    if (m_activeRequests.contains(trackId)) {
        return;
    }

    const LrclibQuery q = lrclibQueryFor(track);
    const QUrl url = lrclibGetUrl(q);

    QNetworkRequest req(url);
    req.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", core::userAgent().toUtf8());
    req.setTransferTimeout(10000);

    QNetworkReply *reply = m_network.get(req);
    m_activeRequests.insert(trackId, reply);

    connect(reply, &QNetworkReply::finished, this,
        [this, trackId, q, reply]() { onGetReplyFinished(trackId, q, reply); });
}

LyricsResult LyricsService::storeAndBuildResult(qint64 trackId, const LrclibRecord &rec)
{
    if (rec.instrumental) {
        const auto saveRes = m_store.saveLrclib(trackId, QString(), false, true, rec.id);
        if (!saveRes.ok()) {
            qCWarning(lcGuide, "Failed to save instrumental lyrics: %s",
                qPrintable(saveRes.error().message));
        }
        return LyricsResult {
            .trackId = trackId,
            .status = LyricsStatus::Instrumental,
            .lyrics = Lyrics { },
        };
    }

    const bool hasSynced = !rec.syncedLyrics.trimmed().isEmpty();
    const QString text = hasSynced ? rec.syncedLyrics : rec.plainLyrics;
    const bool synced = hasSynced;
    const auto saveRes = m_store.saveLrclib(trackId, text, synced, false, rec.id);
    if (!saveRes.ok()) {
        qCWarning(lcGuide, "Failed to save lrclib lyrics: %s", qPrintable(saveRes.error().message));
    }
    return LyricsResult {
        .trackId = trackId,
        .status = LyricsStatus::Found,
        .lyrics = Lyrics {
            .text = text,
            .source = LyricsSource::Lrclib,
            .synced = synced,
        },
    };
}

LyricsResult LyricsService::storeAndBuildNotFoundResult(qint64 trackId)
{
    const auto saveRes = m_store.saveNotFound(trackId);
    if (!saveRes.ok()) {
        qCWarning(
            lcGuide, "Failed to save not found lyrics: %s", qPrintable(saveRes.error().message));
    }
    return LyricsResult {
        .trackId = trackId,
        .status = LyricsStatus::NotFound,
        .lyrics = Lyrics { },
    };
}

void LyricsService::onGetReplyFinished(qint64 trackId, const LrclibQuery &q, QNetworkReply *reply)
{
    if (m_activeRequests.value(trackId) != reply) {
        return;
    }
    m_activeRequests.remove(trackId);
    reply->deleteLater();

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto netErr = reply->error();
    const QByteArray body = reply->readAll();

    const bool isSuccess = (statusCode == 200)
        || (statusCode == 0 && netErr == QNetworkReply::NoError && !body.isEmpty());

    if (isSuccess) {
        const auto parseRes = parseLrclibRecord(body);
        if (parseRes.ok()) {
            m_failedTimestamps.remove(trackId);
            emit finished(storeAndBuildResult(trackId, parseRes.value()));
            return;
        }
    } else if (statusCode == 404) {
        startSearchRequest(trackId, q);
        return;
    }

    m_failedTimestamps.insert(trackId, m_clock.nowMs());
    emit finished(LyricsResult {
        .trackId = trackId,
        .status = LyricsStatus::Failed,
        .lyrics = Lyrics { },
    });
}

void LyricsService::startSearchRequest(qint64 trackId, const LrclibQuery &q)
{
    const QUrl searchUrl = lrclibSearchUrl(q);
    QNetworkRequest req(searchUrl);
    req.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", core::userAgent().toUtf8());
    req.setTransferTimeout(10000);

    QNetworkReply *reply = m_network.get(req);
    m_activeRequests.insert(trackId, reply);

    connect(reply, &QNetworkReply::finished, this,
        [this, trackId, q, reply]() { onSearchReplyFinished(trackId, q, reply); });
}

std::optional<LyricsResult> LyricsService::processSearchPayload(
    qint64 trackId, const LrclibQuery &q, const QByteArray &body)
{
    const auto parseRes = parseLrclibSearch(body);
    if (!parseRes.ok()) {
        return std::nullopt;
    }

    const auto picked = pickSearchResult(parseRes.value(), q);
    if (!picked.has_value()) {
        return storeAndBuildNotFoundResult(trackId);
    }

    return storeAndBuildResult(trackId, picked.value());
}

void LyricsService::onSearchReplyFinished(
    qint64 trackId, const LrclibQuery &q, QNetworkReply *reply)
{
    if (m_activeRequests.value(trackId) != reply) {
        return;
    }
    m_activeRequests.remove(trackId);
    reply->deleteLater();

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto netErr = reply->error();
    const QByteArray body = reply->readAll();

    const bool isSuccess = (statusCode == 200)
        || (statusCode == 0 && netErr == QNetworkReply::NoError && !body.isEmpty());

    if (isSuccess) {
        const auto result = processSearchPayload(trackId, q, body);
        if (result.has_value()) {
            m_failedTimestamps.remove(trackId);
            emit finished(result.value());
            return;
        }
    } else if (statusCode == 404) {
        m_failedTimestamps.remove(trackId);
        emit finished(storeAndBuildNotFoundResult(trackId));
        return;
    }

    m_failedTimestamps.insert(trackId, m_clock.nowMs());
    emit finished(LyricsResult {
        .trackId = trackId,
        .status = LyricsStatus::Failed,
        .lyrics = Lyrics { },
    });
}

core::Result<void> LyricsService::saveManual(qint64 trackId, const QString &text)
{
    auto saveRes = m_store.saveManual(trackId, text);
    if (!saveRes.ok()) {
        return saveRes;
    }
    request(trackId);
    return { };
}

void LyricsService::refetch(qint64 trackId)
{
    const auto it = m_activeRequests.find(trackId);
    if (it != m_activeRequests.end()) {
        QNetworkReply *reply = it.value();
        m_activeRequests.erase(it);
        if (reply != nullptr) {
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
        }
    }
    m_failedTimestamps.remove(trackId);
    const auto removeRes = m_store.removeOnline(trackId);
    if (!removeRes.ok()) {
        qCWarning(lcGuide, "Failed to remove online lyrics on refetch: %s",
            qPrintable(removeRes.error().message));
    }
    request(trackId);
}

} // namespace linernotes::guide
