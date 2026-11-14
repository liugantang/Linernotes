// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QObject>

#include <core/Clock.h>
#include <core/Result.h>
#include <guide/Lrclib.h>
#include <guide/Lyrics.h>
#include <guide/LyricsStore.h>
#include <library/Database.h>
#include <library/LibraryQuery.h>

class QNetworkAccessManager;
class QNetworkReply;

namespace linernotes::guide {

class LyricsService : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(LyricsService)

public:
    explicit LyricsService(library::Database &db, QNetworkAccessManager &network,
        const core::Clock &clock, QObject *parent = nullptr);
    ~LyricsService() override;

    void request(qint64 trackId);
    core::Result<void> saveManual(qint64 trackId, const QString &text);
    void refetch(qint64 trackId);

signals:
    void finished(const linernotes::guide::LyricsResult &result);

private:
    std::optional<LyricsResult> localResult(qint64 trackId, const library::TrackRow &track,
        const std::optional<StoredLyrics> &stored) const;
    std::optional<LyricsResult> cachedResult(
        qint64 trackId, const std::optional<StoredLyrics> &stored) const;
    LyricsResult storeAndBuildResult(qint64 trackId, const LrclibRecord &rec);
    LyricsResult storeAndBuildNotFoundResult(qint64 trackId);
    std::optional<LyricsResult> processSearchPayload(
        qint64 trackId, const LrclibQuery &q, const QByteArray &body);

    void fetchOnline(qint64 trackId, const library::TrackRow &track);
    void onGetReplyFinished(qint64 trackId, const LrclibQuery &q, QNetworkReply *reply);
    void startSearchRequest(qint64 trackId, const LrclibQuery &q);
    void onSearchReplyFinished(qint64 trackId, const LrclibQuery &q, QNetworkReply *reply);

    library::Database &m_db;
    QNetworkAccessManager &m_network;
    const core::Clock &m_clock;
    LyricsStore m_store;
    library::LibraryQuery m_query;

    QHash<qint64, QNetworkReply *> m_activeRequests;
    QHash<qint64, qint64> m_failedTimestamps;
};

} // namespace linernotes::guide
