// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "EndlessPlay.h"

#include <QList>
#include <QSet>

#include <core/Clock.h>
#include <core/PlaySource.h>
#include <core/Settings.h>
#include <library/Database.h>
#include <library/LibraryQuery.h>
#include <player/PlayQueue.h>
#include <rec/Recommender.h>
#include <rec/SessionSeeds.h>
#include <ui/AppSettings.h>
#include <ui/UiLogging.h>

#include <optional>

namespace linernotes::ui {

EndlessPlay::EndlessPlay(library::Database &db, player::PlayQueue &queue,
    rec::Recommender &recommender, core::Settings &settings, const core::Clock &clock,
    QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_queue(queue)
    , m_recommender(recommender)
    , m_settings(settings)
    , m_clock(clock)
{
    connect(&m_queue, &player::PlayQueue::currentIndexChanged, this, &EndlessPlay::maybeExtend);
    connect(&m_queue, &player::PlayQueue::countChanged, this, &EndlessPlay::maybeExtend);
    connect(&m_queue, &player::PlayQueue::modeChanged, this, &EndlessPlay::maybeExtend);
}

bool EndlessPlay::isEnabled() const
{
    return m_settings.value(kQueueEndless);
}

void EndlessPlay::setEnabled(bool enabled)
{
    if (isEnabled() == enabled) {
        return;
    }
    m_settings.setValue(kQueueEndless, enabled);
    emit enabledChanged(enabled);
    if (enabled) {
        m_lastEmptyCount = -1;
        maybeExtend();
    }
}

void EndlessPlay::maybeExtend()
{
    if (m_inExtend) {
        return;
    }
    if (!isEnabled()) {
        return;
    }
    if (m_queue.mode() != player::PlayMode::Sequential) {
        return;
    }
    const int curr = m_queue.currentIndex();
    if (curr < 0) {
        return;
    }
    const int total = m_queue.count();
    const int remaining = total - 1 - curr;
    if (remaining >= 2) {
        return;
    }
    if (m_lastEmptyCount == total) {
        return;
    }
    if (!m_db.isOpen()) {
        return;
    }

    std::optional<qint64> currentTrackId;
    const auto currentItem = m_queue.currentItem();
    if (currentItem.has_value() && currentItem->trackId > 0) {
        currentTrackId = currentItem->trackId;
    }

    const qint64 nowMs = m_clock.nowMs();
    const auto seedRes = rec::sessionSeeds(m_db, nowMs, currentTrackId);
    if (!seedRes.ok()) {
        qCWarning(lcUi, "EndlessPlay failed to get session seeds: %s",
            qPrintable(seedRes.error().toString()));
        return;
    }

    rec::RecRequest req;
    req.seeds = seedRes.value();
    req.count = 10;
    QSet<qint64> excludeIds;
    for (int i = 0; i < m_queue.count(); ++i) {
        const auto &item = m_queue.at(i);
        if (item.trackId > 0) {
            excludeIds.insert(item.trackId);
        }
    }
    req.exclude = excludeIds;
    req.randomness = 0.3;
    req.randomSeed = static_cast<quint64>(nowMs);

    const auto recRes = m_recommender.recommend(req);
    if (!recRes.ok()) {
        qCWarning(
            lcUi, "EndlessPlay recommendation failed: %s", qPrintable(recRes.error().toString()));
        return;
    }
    const auto &trackIds = recRes.value();
    if (trackIds.isEmpty()) {
        m_lastEmptyCount = m_queue.count();
        return;
    }

    const auto connRes = m_db.connection();
    if (!connRes.ok()) {
        qCWarning(
            lcUi, "EndlessPlay db connection failed: %s", qPrintable(connRes.error().toString()));
        return;
    }
    const library::LibraryQuery query(connRes.value());
    const auto tracksRes = query.tracksByIds(trackIds);
    if (!tracksRes.ok()) {
        qCWarning(
            lcUi, "EndlessPlay tracksByIds failed: %s", qPrintable(tracksRes.error().toString()));
        return;
    }
    const auto &rows = tracksRes.value();
    if (rows.isEmpty()) {
        m_lastEmptyCount = m_queue.count();
        return;
    }

    QList<player::QueueItem> items;
    items.reserve(rows.size());
    for (const auto &row : rows) {
        items.append(player::QueueItem {
            .source = row.path,
            .trackId = row.trackId,
            .uid = 0,
            .playSource = core::PlaySource::Queue,
        });
    }

    m_inExtend = true;
    m_queue.append(items);
    m_inExtend = false;
    m_lastEmptyCount = -1;
}

} // namespace linernotes::ui
