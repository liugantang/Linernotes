// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "RecommendController.h"

#include <QDate>
#include <QDateTime>
#include <QStringList>
#include <QVariantMap>

#include <core/Clock.h>
#include <core/PlaySource.h>
#include <core/Settings.h>
#include <library/Database.h>
#include <library/LibraryQuery.h>
#include <rec/Recommender.h>
#include <rec/TasteSeeds.h>
#include <ui/AppSettings.h>
#include <ui/LibraryActions.h>
#include <ui/PlaylistController.h>
#include <ui/TrackRows.h>

namespace linernotes::ui {

RecommendController::RecommendController(library::Database &db, rec::Recommender &recommender,
    PlaylistController &playlists, LibraryActions &actions, core::Settings &settings,
    const core::Clock &clock, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_recommender(recommender)
    , m_playlists(playlists)
    , m_actions(actions)
    , m_settings(settings)
    , m_clock(clock)
{
}

RecommendController::Kind RecommendController::kind() const
{
    return m_kind;
}

void RecommendController::setKind(Kind kind)
{
    if (m_kind == kind) {
        return;
    }
    m_kind = kind;
    load();
}

QVariantList RecommendController::rows() const
{
    return m_kind == Kind::Daily ? m_dailyRows : m_forYouRows;
}

void RecommendController::load()
{
    if (m_kind == Kind::Daily) {
        loadDaily();
    } else {
        loadForYou();
    }
}

bool RecommendController::restoreDaily(const QString &todayStr)
{
    if (m_dailyLoaded && m_dailyDate == todayStr) {
        return true;
    }
    QList<qint64> savedIds;
    const auto parts = m_settings.value(kRecDailyTracks).split(u',', Qt::SkipEmptyParts);
    savedIds.reserve(parts.size());
    for (const auto &part : parts) {
        bool ok = false;
        const qint64 id = part.toLongLong(&ok);
        if (ok && id > 0) {
            savedIds.append(id);
        }
    }
    if (savedIds.isEmpty()) {
        return false;
    }
    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        return false;
    }
    const library::LibraryQuery lq(connRes.value());
    const auto tracksRes = lq.tracksByIds(savedIds);
    if (!tracksRes.ok()) {
        return false;
    }
    m_dailyRows = trackRowsToVariant(tracksRes.value());
    m_dailyLoaded = true;
    m_dailyDate = todayStr;
    return true;
}

void RecommendController::loadDaily()
{
    const qint64 nowMs = m_clock.nowMs();
    const QDate today = QDateTime::fromMSecsSinceEpoch(nowMs).toLocalTime().date();
    const QString todayStr = today.toString(QStringLiteral("yyyy-MM-dd"));

    if (m_settings.value(kRecDailyDate) == todayStr && restoreDaily(todayStr)) {
        emit rowsChanged();
        return;
    }

    const int dateKey = (today.year() * 10000) + (today.month() * 100) + today.day();
    const auto randomSeed = static_cast<quint64>(dateKey);

    rec::RecRequest req;
    req.count = 30;
    req.freshnessWeight = 0.6;
    req.recentExcludeMs = 2LL * 24 * 3600 * 1000;
    req.randomness = 0.8;
    req.randomSeed = randomSeed;

    const auto seedsRes = rec::tasteSeeds(m_db, nowMs);
    if (seedsRes.ok()) {
        req.seeds = seedsRes.value();
    }

    const auto recRes = m_recommender.recommend(req);
    m_dailyRows.clear();
    if (recRes.ok()) {
        const auto &recIds = recRes.value();
        const auto connRes = m_db.connection();
        if (connRes.ok()) {
            const library::LibraryQuery lq(connRes.value());
            const auto tracksRes = lq.tracksByIds(recIds);
            if (tracksRes.ok()) {
                m_dailyRows = trackRowsToVariant(tracksRes.value());
            }
        }

        m_settings.setValue(kRecDailyDate, todayStr);
        QStringList idStrs;
        idStrs.reserve(recIds.size());
        for (const qint64 id : recIds) {
            idStrs.append(QString::number(id));
        }
        m_settings.setValue(kRecDailyTracks, idStrs.join(u','));
    }

    m_dailyLoaded = true;
    m_dailyDate = todayStr;
    emit rowsChanged();
}

void RecommendController::loadForYou()
{
    if (m_forYouLoaded) {
        emit rowsChanged();
        return;
    }

    const qint64 nowMs = m_clock.nowMs();
    m_forYouSeed = static_cast<quint64>(nowMs);

    rec::RecRequest req;
    req.count = 30;
    req.freshnessWeight = 1.5;
    req.recentExcludeMs = 30LL * 24 * 3600 * 1000;
    req.randomness = 0.5;
    req.randomSeed = m_forYouSeed;

    const auto seedsRes = rec::tasteSeeds(m_db, nowMs);
    if (seedsRes.ok()) {
        req.seeds = seedsRes.value();
    }

    const auto recRes = m_recommender.recommend(req);
    m_forYouRows.clear();
    if (recRes.ok()) {
        const auto connRes = m_db.connection();
        if (connRes.ok()) {
            const library::LibraryQuery lq(connRes.value());
            const auto tracksRes = lq.tracksByIds(recRes.value());
            if (tracksRes.ok()) {
                m_forYouRows = trackRowsToVariant(tracksRes.value());
            }
        }
    }

    m_forYouLoaded = true;
    emit rowsChanged();
}

void RecommendController::refresh()
{
    if (m_kind != Kind::ForYou) {
        return;
    }

    const qint64 nowMs = m_clock.nowMs();
    const auto nowSeed = static_cast<quint64>(nowMs);
    m_forYouSeed = (m_forYouSeed == nowSeed) ? (m_forYouSeed + 1) : nowSeed;

    rec::RecRequest req;
    req.count = 30;
    req.freshnessWeight = 1.5;
    req.recentExcludeMs = 30LL * 24 * 3600 * 1000;
    req.randomness = 0.5;
    req.randomSeed = m_forYouSeed;

    const auto seedsRes = rec::tasteSeeds(m_db, nowMs);
    if (seedsRes.ok()) {
        req.seeds = seedsRes.value();
    }

    const auto recRes = m_recommender.recommend(req);
    m_forYouRows.clear();
    if (recRes.ok()) {
        const auto connRes = m_db.connection();
        if (connRes.ok()) {
            const library::LibraryQuery lq(connRes.value());
            const auto tracksRes = lq.tracksByIds(recRes.value());
            if (tracksRes.ok()) {
                m_forYouRows = trackRowsToVariant(tracksRes.value());
            }
        }
    }

    m_forYouLoaded = true;
    emit rowsChanged();
}

QList<qint64> RecommendController::collectResultTrackIds() const
{
    const QVariantList currentRows = rows();
    QList<qint64> trackIds;
    trackIds.reserve(currentRows.size());
    for (const auto &rowVar : currentRows) {
        const auto map = rowVar.toMap();
        trackIds.append(map.value(QStringLiteral("trackId")).toLongLong());
    }
    return trackIds;
}

void RecommendController::playAll()
{
    const QList<qint64> trackIds = collectResultTrackIds();
    if (!trackIds.isEmpty()) {
        m_actions.playTracks(trackIds, 0, core::PlaySource::Playlist);
    }
}

void RecommendController::playRow(int index)
{
    if (index < 0) {
        return;
    }

    const QList<qint64> trackIds = collectResultTrackIds();
    if (index < trackIds.size()) {
        m_actions.playTracks(trackIds, index, core::PlaySource::Playlist);
    }
}

void RecommendController::enqueueAll()
{
    const QList<qint64> trackIds = collectResultTrackIds();
    if (!trackIds.isEmpty()) {
        m_actions.enqueue(trackIds);
    }
}

qint64 RecommendController::saveAsPlaylist(const QString &name)
{
    const QList<qint64> trackIds = collectResultTrackIds();
    if (trackIds.isEmpty()) {
        return -1;
    }
    const qint64 id = m_playlists.createManual(name, trackIds);
    return id > 0 ? id : -1;
}

} // namespace linernotes::ui
