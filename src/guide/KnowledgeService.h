// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QUrl>

#include <core/Clock.h>
#include <guide/GuideEnums.h>
#include <guide/Knowledge.h>
#include <guide/KnowledgeStore.h>
#include <guide/Wikidata.h>
#include <guide/Wikipedia.h>
#include <library/Database.h>

#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace linernotes::guide {

class KnowledgeService : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(KnowledgeService)

public:
    explicit KnowledgeService(library::Database &db, QNetworkAccessManager &network,
        const core::Clock &clock, QObject *parent = nullptr);
    ~KnowledgeService() override;

    void lookup(const KnowledgeQuery &query);

signals:
    void finished(const linernotes::guide::KnowledgeResult &result);

private:
    struct QueuedRequest {
        QUrl url;
        std::function<void(const QByteArray &body, int statusCode, int netErr)> callback;
    };

    struct Job {
        KnowledgeQuery query;
        QString key;

        // Artist resolution
        QString artistKey;
        QString artistName;
        QStringList artistLanguages;
        int currentArtistLangIndex = 0;
        QList<WikidataSearchHit> currentArtistHits;
        QString resolvedArtistQid;

        // Target search
        QString targetSearchText;
        QStringList targetLanguages;
        int currentTargetLangIndex = 0;
        QList<WikidataSearchHit> currentTargetHits;
        WikidataEntity targetEntity;

        // Fact / Summary details
        QHash<QString, WikidataEntity> labelEntities;
        std::optional<WikipediaSummary> summary;
    };

    void enqueueNetworkRequest(
        const QUrl &url, std::function<void(const QByteArray &, int, int)> callback);
    void scheduleProcessNext();
    void processNextNetworkRequest();

    void startLookup(const std::shared_ptr<Job> &job);
    void searchNextArtistLanguage(const std::shared_ptr<Job> &job);
    void onArtistSearchFinished(
        const std::shared_ptr<Job> &job, const QByteArray &body, int statusCode, int netErr);
    void onArtistEntitiesFinished(
        const std::shared_ptr<Job> &job, const QByteArray &body, int statusCode, int netErr);

    void startTargetSearchForSongOrAlbum(const std::shared_ptr<Job> &job);
    void searchNextTargetLanguage(const std::shared_ptr<Job> &job);
    void onTargetSearchFinished(
        const std::shared_ptr<Job> &job, const QByteArray &body, int statusCode, int netErr);
    void onTargetEntitiesFinished(
        const std::shared_ptr<Job> &job, const QByteArray &body, int statusCode, int netErr);

    void fetchTargetDetails(const std::shared_ptr<Job> &job);
    void onReferencedLabelsFinished(
        const std::shared_ptr<Job> &job, const QByteArray &body, int statusCode, int netErr);
    void fetchWikipediaSummaryIfAvailable(const std::shared_ptr<Job> &job);
    void onWikipediaSummaryFinished(const std::shared_ptr<Job> &job, const QByteArray &body,
        int statusCode, int netErr, const QString &lang);
    void finishAndSaveEntry(const std::shared_ptr<Job> &job);

    void handleJobFailed(const std::shared_ptr<Job> &job);
    void handleJobNotFound(const std::shared_ptr<Job> &job);
    void handleJobFound(const std::shared_ptr<Job> &job, const KnowledgeEntry &entry);

    library::Database &m_db;
    QNetworkAccessManager &m_network;
    const core::Clock &m_clock;
    KnowledgeStore m_store;

    QTimer *m_queueTimer = nullptr;
    QQueue<QueuedRequest> m_networkQueue;
    QNetworkReply *m_currentReply = nullptr;
    qint64 m_lastRequestStartMs = 0;
    qint64 m_cooldownUntilMs = 0;

    QSet<QString> m_inFlightKeys;
    QHash<QString, qint64> m_failedTimestamps;
};

} // namespace linernotes::guide
