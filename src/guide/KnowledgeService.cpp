// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "KnowledgeService.h"

#include "GuideLogging.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#include <butler/TitleVersion.h>
#include <core/Version.h>

#include <algorithm>

namespace linernotes::guide {

namespace {

constexpr qint64 kMinIntervalMs = 1000;
constexpr qint64 kRateLimitCooldownMs = 60000;
constexpr qint64 kFailCooldownMs = 10LL * 60 * 1000;
constexpr int kTransferTimeoutMs = 15000;

QString preferredDescription(const WikidataEntity &entity)
{
    static const auto s_langs = {
        QStringLiteral("zh"),
        QStringLiteral("ja"),
        QStringLiteral("en"),
    };
    for (const auto &lang : s_langs) {
        const auto it = entity.descriptions.find(lang);
        if (it != entity.descriptions.end() && !it.value().isEmpty()) {
            return it.value();
        }
    }
    return { };
}

} // namespace

KnowledgeService::KnowledgeService(library::Database &db, QNetworkAccessManager &network,
    const core::Clock &clock, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_network(network)
    , m_clock(clock)
    , m_store(db, clock)
    , m_queueTimer(new QTimer(this))
{
    m_queueTimer->setSingleShot(true);
    connect(m_queueTimer, &QTimer::timeout, this, &KnowledgeService::processNextNetworkRequest);
}

KnowledgeService::~KnowledgeService()
{
    if (m_queueTimer != nullptr) {
        m_queueTimer->stop();
    }
    if (m_currentReply != nullptr) {
        m_currentReply->disconnect(this);
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    m_networkQueue.clear();
    m_inFlightKeys.clear();
}

void KnowledgeService::lookup(const KnowledgeQuery &query)
{
    const QString primary = primaryArtist(query.artist);
    if (primary.isEmpty()) {
        emit finished(KnowledgeResult {
            .key = knowledgeKey(query),
            .status = KnowledgeStatus::NotFound,
            .entry = { },
        });
        return;
    }

    if (query.kind != KnowledgeKind::Artist && query.title.trimmed().isEmpty()) {
        emit finished(KnowledgeResult {
            .key = knowledgeKey(query),
            .status = KnowledgeStatus::NotFound,
            .entry = { },
        });
        return;
    }

    const QString key = knowledgeKey(query);

    const auto cachedRes = m_store.get(key);
    if (cachedRes.ok()) {
        const auto &cached = cachedRes.value();
        if (cached.state == LookupState::Found) {
            emit finished(KnowledgeResult {
                .key = key,
                .status = KnowledgeStatus::Found,
                .entry = cached.entry,
            });
            return;
        }
        if (cached.state == LookupState::NotFound) {
            emit finished(KnowledgeResult {
                .key = key,
                .status = KnowledgeStatus::NotFound,
                .entry = { },
            });
            return;
        }
    }

    const auto failIt = m_failedTimestamps.constFind(key);
    if (failIt != m_failedTimestamps.constEnd()) {
        if (m_clock.nowMs() - failIt.value() < kFailCooldownMs) {
            emit finished(KnowledgeResult {
                .key = key,
                .status = KnowledgeStatus::Failed,
                .entry = { },
            });
            return;
        }
    }

    if (m_inFlightKeys.contains(key)) {
        return;
    }
    m_inFlightKeys.insert(key);

    auto job = std::make_shared<Job>();
    job->query = query;
    job->key = key;

    startLookup(job);
}

void KnowledgeService::enqueueNetworkRequest(
    const QUrl &url, std::function<void(const QByteArray &, int, int)> callback)
{
    m_networkQueue.enqueue(QueuedRequest {
        .url = url,
        .callback = std::move(callback),
    });
    scheduleProcessNext();
}

void KnowledgeService::scheduleProcessNext()
{
    if (m_currentReply != nullptr || m_networkQueue.isEmpty()) {
        return;
    }

    const qint64 now = m_clock.nowMs();
    const qint64 earliestAllowed
        = std::max(m_lastRequestStartMs + kMinIntervalMs, m_cooldownUntilMs);
    const qint64 delayMs = std::max(0LL, earliestAllowed - now);

    if (delayMs <= 0) {
        processNextNetworkRequest();
    } else if (!m_queueTimer->isActive()) {
        m_queueTimer->start(static_cast<int>(delayMs));
    }
}

void KnowledgeService::processNextNetworkRequest()
{
    if (m_currentReply != nullptr || m_networkQueue.isEmpty()) {
        return;
    }

    const qint64 now = m_clock.nowMs();
    const qint64 earliestAllowed
        = std::max(m_lastRequestStartMs + kMinIntervalMs, m_cooldownUntilMs);
    const qint64 delayMs = std::max(0LL, earliestAllowed - now);
    if (delayMs > 0) {
        m_queueTimer->start(static_cast<int>(delayMs));
        return;
    }

    const QueuedRequest req = m_networkQueue.dequeue();
    m_lastRequestStartMs = m_clock.nowMs();

    QNetworkRequest netReq(req.url);
    netReq.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    netReq.setRawHeader("User-Agent", core::userAgent().toUtf8());
    netReq.setTransferTimeout(kTransferTimeoutMs);

    m_currentReply = m_network.get(netReq);
    connect(m_currentReply, &QNetworkReply::finished, this, [this, callback = req.callback]() {
        if (m_currentReply == nullptr) {
            return;
        }
        QNetworkReply *reply = m_currentReply;
        m_currentReply = nullptr;
        reply->deleteLater();

        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const int netErr = static_cast<int>(reply->error());
        const QByteArray body = reply->readAll();

        if (statusCode == 429) {
            m_cooldownUntilMs = m_clock.nowMs() + kRateLimitCooldownMs;
        }

        callback(body, statusCode, netErr);
        scheduleProcessNext();
    });
}

void KnowledgeService::startLookup(const std::shared_ptr<Job> &job)
{
    if (job->query.kind == KnowledgeKind::Artist) {
        job->artistName = primaryArtist(job->query.artist);
        job->targetSearchText = job->artistName;
        job->targetLanguages = searchLanguagesFor(job->targetSearchText);
        job->currentTargetLangIndex = 0;
        searchNextTargetLanguage(job);
        return;
    }

    job->artistName = primaryArtist(job->query.artist);
    job->artistKey = knowledgeKey(KnowledgeQuery {
        .kind = KnowledgeKind::Artist,
        .artist = job->query.artist,
        .title = { },
    });

    const auto artistLookupRes = m_store.get(job->artistKey);
    if (artistLookupRes.ok()) {
        const auto &cachedArtist = artistLookupRes.value();
        if (cachedArtist.state == LookupState::NotFound) {
            handleJobNotFound(job);
            return;
        }
        if (cachedArtist.state == LookupState::Found) {
            job->resolvedArtistQid = cachedArtist.entry.qid;
            startTargetSearchForSongOrAlbum(job);
            return;
        }
    }

    job->artistLanguages = searchLanguagesFor(job->artistName);
    job->currentArtistLangIndex = 0;
    searchNextArtistLanguage(job);
}

void KnowledgeService::searchNextArtistLanguage(const std::shared_ptr<Job> &job)
{
    if (job->currentArtistLangIndex >= job->artistLanguages.size()) {
        if (const auto saveRes = m_store.saveNotFound(job->artistKey); !saveRes.ok()) {
            qCWarning(lcGuide) << "Failed to cache artist not-found:" << saveRes.error().message;
        }
        handleJobNotFound(job);
        return;
    }

    const QString &lang = job->artistLanguages.at(job->currentArtistLangIndex);
    const QUrl url = wikidataSearchUrl(job->artistName, lang);
    enqueueNetworkRequest(
        url, [this, job](const QByteArray &replyBody, int replyStatus, int replyError) {
            onArtistSearchFinished(job, replyBody, replyStatus, replyError);
        });
}

void KnowledgeService::onArtistSearchFinished(
    const std::shared_ptr<Job> &job, const QByteArray &body, int statusCode, int netErr)
{
    if (netErr != 0 || statusCode != 200) {
        handleJobFailed(job);
        return;
    }

    const auto parseRes = parseWikidataSearch(body);
    if (!parseRes.ok()) {
        handleJobFailed(job);
        return;
    }

    const auto &hits = parseRes.value();
    if (hits.isEmpty()) {
        ++job->currentArtistLangIndex;
        searchNextArtistLanguage(job);
        return;
    }

    job->currentArtistHits = hits;
    QStringList hitIds;
    hitIds.reserve(hits.size());
    for (const auto &hit : hits) {
        hitIds.append(hit.id);
    }

    const QUrl url = wikidataEntitiesUrl(hitIds);
    enqueueNetworkRequest(
        url, [this, job](const QByteArray &replyBody, int replyStatus, int replyError) {
            onArtistEntitiesFinished(job, replyBody, replyStatus, replyError);
        });
}

void KnowledgeService::onArtistEntitiesFinished(
    const std::shared_ptr<Job> &job, const QByteArray &body, int statusCode, int netErr)
{
    if (netErr != 0 || statusCode != 200) {
        handleJobFailed(job);
        return;
    }

    const auto parseRes = parseWikidataEntities(body);
    if (!parseRes.ok()) {
        handleJobFailed(job);
        return;
    }

    const auto &entities = parseRes.value();
    const auto pickedQid = pickArtist(job->currentArtistHits, entities);
    if (!pickedQid.has_value()) {
        ++job->currentArtistLangIndex;
        searchNextArtistLanguage(job);
        return;
    }

    job->resolvedArtistQid = pickedQid.value();
    startTargetSearchForSongOrAlbum(job);
}

void KnowledgeService::startTargetSearchForSongOrAlbum(const std::shared_ptr<Job> &job)
{
    const auto resolved = butler::resolveTitle(job->query.title, butler::classifySuffix);
    job->targetSearchText = resolved.baseTitle;
    job->targetLanguages = searchLanguagesFor(job->targetSearchText);
    job->currentTargetLangIndex = 0;
    searchNextTargetLanguage(job);
}

void KnowledgeService::searchNextTargetLanguage(const std::shared_ptr<Job> &job)
{
    if (job->currentTargetLangIndex >= job->targetLanguages.size()) {
        handleJobNotFound(job);
        return;
    }

    const QString &lang = job->targetLanguages.at(job->currentTargetLangIndex);
    const QUrl url = wikidataSearchUrl(job->targetSearchText, lang);
    enqueueNetworkRequest(
        url, [this, job](const QByteArray &replyBody, int replyStatus, int replyError) {
            onTargetSearchFinished(job, replyBody, replyStatus, replyError);
        });
}

void KnowledgeService::onTargetSearchFinished(
    const std::shared_ptr<Job> &job, const QByteArray &body, int statusCode, int netErr)
{
    if (netErr != 0 || statusCode != 200) {
        handleJobFailed(job);
        return;
    }

    const auto parseRes = parseWikidataSearch(body);
    if (!parseRes.ok()) {
        handleJobFailed(job);
        return;
    }

    const auto &hits = parseRes.value();
    if (hits.isEmpty()) {
        ++job->currentTargetLangIndex;
        searchNextTargetLanguage(job);
        return;
    }

    job->currentTargetHits = hits;
    QStringList hitIds;
    hitIds.reserve(hits.size());
    for (const auto &hit : hits) {
        hitIds.append(hit.id);
    }

    const QUrl url = wikidataEntitiesUrl(hitIds);
    enqueueNetworkRequest(
        url, [this, job](const QByteArray &replyBody, int replyStatus, int replyError) {
            onTargetEntitiesFinished(job, replyBody, replyStatus, replyError);
        });
}

void KnowledgeService::onTargetEntitiesFinished(
    const std::shared_ptr<Job> &job, const QByteArray &body, int statusCode, int netErr)
{
    if (netErr != 0 || statusCode != 200) {
        handleJobFailed(job);
        return;
    }

    const auto parseRes = parseWikidataEntities(body);
    if (!parseRes.ok()) {
        handleJobFailed(job);
        return;
    }

    const auto &entities = parseRes.value();
    std::optional<QString> pickedQid;
    if (job->query.kind == KnowledgeKind::Artist) {
        pickedQid = pickArtist(job->currentTargetHits, entities);
    } else if (job->query.kind == KnowledgeKind::Song) {
        pickedQid = pickSong(job->currentTargetHits, entities, job->resolvedArtistQid);
    } else if (job->query.kind == KnowledgeKind::Album) {
        pickedQid = pickAlbum(job->currentTargetHits, entities, job->resolvedArtistQid);
    }

    if (!pickedQid.has_value()) {
        ++job->currentTargetLangIndex;
        searchNextTargetLanguage(job);
        return;
    }

    job->targetEntity = entities.value(pickedQid.value());
    fetchTargetDetails(job);
}

void KnowledgeService::fetchTargetDetails(const std::shared_ptr<Job> &job)
{
    const QStringList refIds = referencedIds(job->targetEntity, job->query.kind);
    if (!refIds.isEmpty()) {
        const QStringList idsToFetch = refIds.mid(0, 50);
        const QUrl url = wikidataLabelsUrl(idsToFetch);
        enqueueNetworkRequest(
            url, [this, job](const QByteArray &replyBody, int replyStatus, int replyError) {
                onReferencedLabelsFinished(job, replyBody, replyStatus, replyError);
            });
    } else {
        job->labelEntities.clear();
        fetchWikipediaSummaryIfAvailable(job);
    }
}

void KnowledgeService::onReferencedLabelsFinished(
    const std::shared_ptr<Job> &job, const QByteArray &body, int statusCode, int netErr)
{
    if (netErr != 0 || statusCode != 200) {
        handleJobFailed(job);
        return;
    }

    const auto parseRes = parseWikidataEntities(body);
    if (!parseRes.ok()) {
        handleJobFailed(job);
        return;
    }

    job->labelEntities = parseRes.value();
    fetchWikipediaSummaryIfAvailable(job);
}

void KnowledgeService::fetchWikipediaSummaryIfAvailable(const std::shared_ptr<Job> &job)
{
    const auto sitelink = preferredSitelink(job->targetEntity);
    if (sitelink.has_value()) {
        const QString lang = sitelink->first;
        const QString title = sitelink->second;
        const QUrl url = wikipediaSummaryUrl(lang, title);
        enqueueNetworkRequest(
            url, [this, job, lang](const QByteArray &body, int statusCode, int netErr) {
                onWikipediaSummaryFinished(job, body, statusCode, netErr, lang);
            });
    } else {
        job->summary = std::nullopt;
        finishAndSaveEntry(job);
    }
}

void KnowledgeService::onWikipediaSummaryFinished(const std::shared_ptr<Job> &job,
    const QByteArray &body, int statusCode, int netErr, const QString &lang)
{
    if (netErr == 0 && statusCode == 200) {
        const auto summaryRes = parseWikipediaSummary(body, lang);
        if (summaryRes.ok()) {
            job->summary = summaryRes.value();
        } else {
            job->summary = std::nullopt;
        }
    } else {
        job->summary = std::nullopt;
    }

    finishAndSaveEntry(job);
}

void KnowledgeService::finishAndSaveEntry(const std::shared_ptr<Job> &job)
{
    const auto facts = extractFacts(job->targetEntity, job->query.kind, job->labelEntities);

    KnowledgeEntry entry;
    entry.kind = job->query.kind;
    entry.qid = job->targetEntity.id;
    entry.label = preferredLabel(job->targetEntity);
    entry.description = preferredDescription(job->targetEntity);
    entry.wikidataUrl = wikidataEntityPageUrl(job->targetEntity.id);
    entry.summary = job->summary;
    entry.facts = facts;
    entry.fetchedAtMs = m_clock.nowMs();

    handleJobFound(job, entry);
}

void KnowledgeService::handleJobFailed(const std::shared_ptr<Job> &job)
{
    m_failedTimestamps.insert(job->key, m_clock.nowMs());
    m_inFlightKeys.remove(job->key);

    emit finished(KnowledgeResult {
        .key = job->key,
        .status = KnowledgeStatus::Failed,
        .entry = { },
    });
}

void KnowledgeService::handleJobNotFound(const std::shared_ptr<Job> &job)
{
    const auto saveRes = m_store.saveNotFound(job->key);
    if (!saveRes.ok()) {
        qCWarning(lcGuide, "Failed to save negative knowledge cache: %s",
            qPrintable(saveRes.error().message));
    }
    m_failedTimestamps.remove(job->key);
    m_inFlightKeys.remove(job->key);

    emit finished(KnowledgeResult {
        .key = job->key,
        .status = KnowledgeStatus::NotFound,
        .entry = { },
    });
}

void KnowledgeService::handleJobFound(const std::shared_ptr<Job> &job, const KnowledgeEntry &entry)
{
    const auto saveRes = m_store.saveFound(job->key, entry);
    if (!saveRes.ok()) {
        qCWarning(
            lcGuide, "Failed to save knowledge entry: %s", qPrintable(saveRes.error().message));
    }
    m_failedTimestamps.remove(job->key);
    m_inFlightKeys.remove(job->key);

    emit finished(KnowledgeResult {
        .key = job->key,
        .status = KnowledgeStatus::Found,
        .entry = entry,
    });
}

} // namespace linernotes::guide
