// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "CoverSearchController.h"

#include "UiLogging.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSqlQuery>
#include <QVariantMap>

#include <butler/ItunesSearch.h>
#include <core/Clock.h>
#include <core/Version.h>
#include <library/AlbumCovers.h>
#include <library/CoverStore.h>
#include <library/Database.h>

#include <utility>

namespace linernotes::ui {

namespace {

QString appUserAgent()
{
    return QStringLiteral("Linernotes/%1 ( https://github.com/linernotes/linernotes )")
        .arg(core::versionString());
}

} // namespace

CoverSearchController::CoverSearchController(library::Database &db, QNetworkAccessManager &network,
    library::CoverStore &coverStore, const core::Clock &clock, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_network(network)
    , m_coverStore(coverStore)
    , m_clock(clock)
{
}

CoverSearchController::~CoverSearchController()
{
    cancelInternal();
}

bool CoverSearchController::isBusy() const
{
    return m_busy;
}

QVariantList CoverSearchController::results() const
{
    return m_results;
}

QString CoverSearchController::errorText() const
{
    return m_errorText;
}

void CoverSearchController::cancelInternal()
{
    m_pendingTargets.clear();
    if (m_currentReply != nullptr) {
        m_currentReply->disconnect(this);
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    if (m_downloadReply != nullptr) {
        m_downloadReply->disconnect(this);
        m_downloadReply->abort();
        m_downloadReply->deleteLater();
        m_downloadReply = nullptr;
    }
}

void CoverSearchController::cancel()
{
    cancelInternal();
    if (m_busy) {
        m_busy = false;
        emit busyChanged();
    }
}

void CoverSearchController::search(qint64 albumId)
{
    cancelInternal();
    m_albumId = albumId;
    m_albums.clear();
    m_results.clear();
    m_errorText.clear();
    m_totalCount = 0;
    m_failedCount = 0;
    emit resultsChanged();
    emit errorTextChanged();

    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        m_errorText = tr("Database error: %1").arg(connRes.error().message);
        emit errorTextChanged();
        return;
    }
    const auto &conn = connRes.value();

    QSqlQuery q(conn);
    q.prepare(QStringLiteral("SELECT title, album_artist FROM albums WHERE id = ?;"));
    q.addBindValue(albumId);
    if (!q.exec() || !q.next()) {
        m_errorText = tr("Album not found");
        emit errorTextChanged();
        return;
    }

    const QString title = q.value(0).toString();
    const QString artist = q.value(1).toString();

    const QStringList terms = butler::itunesSearchTerms(title, artist);
    if (terms.isEmpty()) {
        m_errorText = tr("No search terms available");
        emit errorTextChanged();
        return;
    }

    m_pendingTargets.clear();
    for (const auto &term : terms) {
        for (const auto *country : butler::kItunesCountries) {
            m_pendingTargets.append(SearchTarget {
                .term = term,
                .country = QString::fromLatin1(country),
            });
        }
    }

    m_totalCount = static_cast<int>(m_pendingTargets.size());
    m_failedCount = 0;
    m_busy = true;
    emit busyChanged();

    startNextSearchRequest();
}

void CoverSearchController::startNextSearchRequest()
{
    if (m_pendingTargets.isEmpty()) {
        m_busy = false;
        emit busyChanged();
        if (m_albums.isEmpty() && m_failedCount == m_totalCount && m_totalCount > 0) {
            m_errorText = tr("Network request failed");
            emit errorTextChanged();
        }
        return;
    }

    const auto target = m_pendingTargets.takeFirst();
    const QUrl url = butler::itunesSearchUrl(target.term, target.country);

    QNetworkRequest req(url);
    req.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", appUserAgent().toUtf8());
    req.setTransferTimeout(15000);

    m_currentReply = m_network.get(req);
    connect(m_currentReply, &QNetworkReply::finished, this,
        [this, country = target.country]() { onSearchReplyFinished(country); });
}

void CoverSearchController::onSearchReplyFinished(const QString &country)
{
    if (m_currentReply == nullptr) {
        return;
    }

    QNetworkReply *reply = m_currentReply;
    m_currentReply = nullptr;
    reply->deleteLater();

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto netErr = reply->error();
    const QByteArray body = reply->readAll();

    const bool isSuccess = (statusCode == 200)
        || (statusCode == 0 && netErr == QNetworkReply::NoError && !body.isEmpty());

    if (!isSuccess) {
        m_failedCount++;
        qCWarning(lcUi, "iTunes search failed for country %s: HTTP %d, error %s",
            qPrintable(country), statusCode, qPrintable(reply->errorString()));
    } else {
        const auto parseRes = butler::parseItunesSearch(body, country);
        if (!parseRes.ok()) {
            m_failedCount++;
            qCWarning(lcUi, "Failed to parse iTunes search results for country %s: %s",
                qPrintable(country), qPrintable(parseRes.error().toString()));
        } else {
            const auto &newAlbums = parseRes.value();
            if (!newAlbums.isEmpty()) {
                const auto oldSize = m_albums.size();
                m_albums = butler::mergeItunesResults({ m_albums, newAlbums });
                if (m_albums.size() != oldSize) {
                    updateResultsProperty();
                }
            }
        }
    }

    startNextSearchRequest();
}

void CoverSearchController::updateResultsProperty()
{
    QVariantList list;
    list.reserve(m_albums.size());
    for (const auto &alb : m_albums) {
        QVariantMap map;
        map.insert(QStringLiteral("collectionId"), alb.collectionId);
        map.insert(QStringLiteral("title"), alb.title);
        map.insert(QStringLiteral("artist"), alb.artist);
        map.insert(
            QStringLiteral("year"), alb.year.has_value() ? QVariant(alb.year.value()) : QVariant());
        map.insert(QStringLiteral("trackCount"), alb.trackCount);
        map.insert(QStringLiteral("country"), alb.country.toUpper());
        map.insert(QStringLiteral("thumbnailUrl"),
            butler::itunesArtworkUrl(alb.artworkUrl100, 300).toString());
        list.append(map);
    }
    m_results = std::move(list);
    emit resultsChanged();
}

void CoverSearchController::choose(int index)
{
    if (index < 0 || index >= m_albums.size()) {
        return;
    }

    const auto &selectedAlbum = m_albums.at(index);
    const QUrl downloadUrl = butler::itunesArtworkUrl(selectedAlbum.artworkUrl100, 1400);

    cancelInternal();
    m_busy = true;
    m_errorText.clear();
    emit busyChanged();
    emit errorTextChanged();

    QNetworkRequest req(downloadUrl);
    req.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", appUserAgent().toUtf8());
    req.setTransferTimeout(30000);

    m_downloadReply = m_network.get(req);
    connect(m_downloadReply, &QNetworkReply::finished, this,
        [this, downloadUrl]() { onDownloadReplyFinished(downloadUrl); });
}

void CoverSearchController::onDownloadReplyFinished(const QUrl &downloadUrl)
{
    if (m_downloadReply == nullptr) {
        return;
    }

    QNetworkReply *reply = m_downloadReply;
    m_downloadReply = nullptr;
    reply->deleteLater();

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto netErr = reply->error();
    const QByteArray body = reply->readAll();

    const bool isSuccess = (statusCode == 200)
        || (statusCode == 0 && netErr == QNetworkReply::NoError && !body.isEmpty());

    if (!isSuccess) {
        m_busy = false;
        m_errorText = tr("Failed to download cover image");
        emit busyChanged();
        emit errorTextChanged();
        return;
    }

    const auto ingestRes = m_coverStore.ingest(body);
    if (!ingestRes.ok()) {
        m_busy = false;
        m_errorText = tr("Failed to decode cover image");
        emit busyChanged();
        emit errorTextChanged();
        return;
    }

    const auto saveRes = library::setOnlineAlbumCover(
        m_db, m_albumId, ingestRes.value(), downloadUrl, m_clock.nowMs());
    if (!saveRes.ok()) {
        m_busy = false;
        m_errorText = tr("Failed to save cover to database");
        emit busyChanged();
        emit errorTextChanged();
        return;
    }

    m_busy = false;
    emit busyChanged();
    emit coverChanged(m_albumId);
}

} // namespace linernotes::ui
