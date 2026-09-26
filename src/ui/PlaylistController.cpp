// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <library/Database.h>
#include <player/PlayQueue.h>
#include <player/Player.h>
#include <ui/PlaylistController.h>
#include <ui/UiLogging.h>

namespace linernotes::ui {

PlaylistController::PlaylistController(library::Database &db, player::Player &player)
    : m_store(db)
    , m_player(player)
{
}

PlaylistListModel *PlaylistController::model()
{
    return &m_model;
}

void PlaylistController::refresh()
{
    const auto listRes = m_store.list();
    if (!listRes.ok()) {
        qCWarning(lcUi, "Failed to list playlists: %s", qPrintable(listRes.error().toString()));
        return;
    }
    m_model.refresh(listRes.value());
}

qint64 PlaylistController::createManual(const QString &name, const QList<qint64> &trackIds)
{
    const auto res = m_store.createManual(name, trackIds);
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to create playlist '%s': %s", qPrintable(name),
            qPrintable(res.error().toString()));
        return 0;
    }

    refresh();
    emit playlistsChanged();
    return res.value();
}

qint64 PlaylistController::saveQueue(const QString &name)
{
    auto *queue = m_player.queue();
    QList<qint64> trackIds;
    const int count = queue->count();
    trackIds.reserve(count);
    for (int i = 0; i < count; ++i) {
        const auto &item = queue->at(i);
        if (item.trackId >= 0) {
            trackIds.append(item.trackId);
        }
    }

    return createManual(name, trackIds);
}

bool PlaylistController::rename(qint64 id, const QString &name)
{
    const auto res = m_store.rename(id, name);
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to rename playlist %lld: %s", static_cast<long long>(id),
            qPrintable(res.error().toString()));
        return false;
    }

    refresh();
    emit playlistsChanged();
    return true;
}

bool PlaylistController::remove(qint64 id)
{
    const auto res = m_store.remove(id);
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to remove playlist %lld: %s", static_cast<long long>(id),
            qPrintable(res.error().toString()));
        return false;
    }

    refresh();
    emit playlistsChanged();
    return true;
}

int PlaylistController::addTracks(qint64 id, const QList<qint64> &trackIds)
{
    const auto res = m_store.addTracks(id, trackIds);
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to add tracks to playlist %lld: %s", static_cast<long long>(id),
            qPrintable(res.error().toString()));
        return -1;
    }

    emit playlistContentChanged(id);
    return res.value();
}

bool PlaylistController::removeTracks(qint64 id, const QList<qint64> &trackIds)
{
    const auto res = m_store.removeTracks(id, trackIds);
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to remove tracks from playlist %lld: %s",
            static_cast<long long>(id), qPrintable(res.error().toString()));
        return false;
    }

    emit playlistContentChanged(id);
    return true;
}

std::optional<library::PlaylistInfo> PlaylistController::info(qint64 id) const
{
    if (id <= 0) {
        return std::nullopt;
    }

    const auto res = m_store.playlist(id);
    if (!res.ok() || !res.value().has_value()) {
        return std::nullopt;
    }
    return res.value();
}

bool PlaylistController::isManual(qint64 id) const
{
    const auto p = info(id);
    return p.has_value() && p->kind == library::PlaylistKind::Manual;
}

} // namespace linernotes::ui
