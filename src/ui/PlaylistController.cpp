// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <QMetaEnum>
#include <QVariant>

#include <library/Database.h>
#include <library/LibraryQuery.h>
#include <library/SmartRule.h>
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
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        qCWarning(lcUi, "Failed to create manual playlist: empty name");
        return 0;
    }

    const auto res = m_store.createManual(trimmed, trackIds);
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to create playlist '%s': %s", qPrintable(name),
            qPrintable(res.error().toString()));
        return 0;
    }

    refresh();
    emit playlistsChanged();
    return res.value();
}

qint64 PlaylistController::createSmart(const QString &name, const library::SmartRule &rule)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        qCWarning(lcUi, "Failed to create smart playlist: empty name");
        return 0;
    }

    const auto res = m_store.createSmart(trimmed, rule);
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to create smart playlist '%s': %s", qPrintable(name),
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
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        qCWarning(lcUi, "Failed to rename playlist %lld: empty name", static_cast<long long>(id));
        return false;
    }

    const auto res = m_store.rename(id, trimmed);
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

bool PlaylistController::moveTracks(qint64 id, const QList<qint64> &trackIds, int beforePosition)
{
    const auto res = m_store.moveTracks(id, trackIds, beforePosition);
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to move tracks in playlist %lld: %s", static_cast<long long>(id),
            qPrintable(res.error().toString()));
        return false;
    }

    emit playlistContentChanged(id);
    return true;
}

bool PlaylistController::setRule(qint64 id, const library::SmartRule &rule)
{
    const auto res = m_store.setRule(id, rule);
    if (!res.ok()) {
        qCWarning(lcUi, "Failed to set rule for playlist %lld: %s", static_cast<long long>(id),
            qPrintable(res.error().toString()));
        return false;
    }

    refresh();
    emit playlistsChanged();
    emit playlistContentChanged(id);
    return true;
}

library::SmartRule PlaylistController::rule(qint64 id) const
{
    const auto p = info(id);
    if (p.has_value() && p->rule.has_value()) {
        return p->rule.value();
    }
    return { };
}

QVariantList PlaylistController::smartFields() const
{
    QVariantList list;
    const auto metaEnum = QMetaEnum::fromType<library::SmartField>();
    for (int i = 0; i < metaEnum.keyCount(); ++i) {
        list.append(metaEnum.value(i));
    }
    return list;
}

QVariantList PlaylistController::smartOps(library::SmartField field) const
{
    QVariantList list;
    for (auto op : library::smartOpsFor(field)) {
        list.append(QVariant::fromValue(op));
    }
    return list;
}

library::SmartFieldKind PlaylistController::smartFieldKind(library::SmartField field) const
{
    return library::smartFieldKind(field);
}

QVariantList PlaylistController::smartSortKeys() const
{
    QVariantList list;
    const auto metaEnum = QMetaEnum::fromType<library::TrackSortKey>();
    for (int i = 0; i < metaEnum.keyCount(); ++i) {
        const auto val = static_cast<library::TrackSortKey>(metaEnum.value(i));
        if (val != library::TrackSortKey::PlaylistOrder) {
            list.append(metaEnum.value(i));
        }
    }
    return list;
}

QString PlaylistController::fieldLabel(library::SmartField field) const
{
    switch (field) {
    case library::SmartField::Title:
        //: Smart playlist condition field: Track title
        return tr("Title");
    case library::SmartField::Artist:
        //: Smart playlist condition field: Track artist
        return tr("Artist");
    case library::SmartField::Album:
        //: Smart playlist condition field: Album name
        return tr("Album");
    case library::SmartField::AlbumArtist:
        //: Smart playlist condition field: Album artist
        return tr("Album Artist");
    case library::SmartField::Genre:
        //: Smart playlist condition field: Music genre
        return tr("Genre");
    case library::SmartField::Year:
        //: Smart playlist condition field: Release year
        return tr("Year");
    case library::SmartField::Codec:
        //: Smart playlist condition field: Audio codec
        return tr("Codec");
    case library::SmartField::Rating:
        //: Smart playlist condition field: Track rating
        return tr("Rating");
    case library::SmartField::Favorite:
        //: Smart playlist condition field: Favorite / loved status
        return tr("Favorite");
    case library::SmartField::DateAdded:
        //: Smart playlist condition field: Date added to library
        return tr("Date Added");
    case library::SmartField::DurationSec:
        //: Smart playlist condition field: Track duration
        return tr("Duration");
    case library::SmartField::PlayCount:
        //: Smart playlist condition field: Play count
        return tr("Play count");
    case library::SmartField::SkipCount:
        //: Smart playlist condition field: Skip count
        return tr("Skip count");
    case library::SmartField::CompletedCount:
        //: Smart playlist condition field: Times completed
        return tr("Times completed");
    case library::SmartField::LastPlayed:
        //: Smart playlist condition field: Last played
        return tr("Last played");
    case library::SmartField::VersionType:
        //: Smart playlist condition field: Version
        return tr("Version");
    case library::SmartField::Language:
        //: Smart playlist condition field: Language
        return tr("Language");
    case library::SmartField::AlbumFavorite:
        //: Smart playlist condition field: Album is favorite
        return tr("Album is favorite");
    case library::SmartField::ArtistFavorite:
        //: Smart playlist condition field: Artist is favorite
        return tr("Artist is favorite");
    case library::SmartField::AlbumCompletion:
        //: Smart playlist condition field: Album completion (%)
        return tr("Album completion (%)");
    }
    Q_UNREACHABLE_RETURN(QString());
}

QString PlaylistController::opLabel(library::SmartOp op) const
{
    switch (op) {
    case library::SmartOp::Contains:
        return tr("Contains");
    case library::SmartOp::NotContains:
        return tr("Does Not Contain");
    case library::SmartOp::Is:
        return tr("Is");
    case library::SmartOp::IsNot:
        return tr("Is Not");
    case library::SmartOp::StartsWith:
        return tr("Starts With");
    case library::SmartOp::Equals:
        return tr("Equals");
    case library::SmartOp::NotEquals:
        return tr("Does Not Equal");
    case library::SmartOp::Greater:
        return tr("Greater Than");
    case library::SmartOp::Less:
        return tr("Less Than");
    case library::SmartOp::Between:
        return tr("Between");
    case library::SmartOp::IsTrue:
        return tr("Is True");
    case library::SmartOp::IsFalse:
        return tr("Is False");
    case library::SmartOp::InLastDays:
        return tr("In the Last (Days)");
    case library::SmartOp::NotInLastDays:
        return tr("Not in the Last (Days)");
    }
    Q_UNREACHABLE_RETURN(QString());
}

QString PlaylistController::sortKeyLabel(library::TrackSortKey key) const
{
    switch (key) {
    case library::TrackSortKey::Default:
        //: Default track sorting order
        return tr("Default");
    case library::TrackSortKey::Title:
        //: Sort by track title
        return tr("Title");
    case library::TrackSortKey::Artist:
        //: Sort by track artist
        return tr("Artist");
    case library::TrackSortKey::Album:
        //: Sort by album name
        return tr("Album");
    case library::TrackSortKey::Year:
        //: Sort by release year
        return tr("Year");
    case library::TrackSortKey::Duration:
        //: Sort by track duration
        return tr("Duration");
    case library::TrackSortKey::DateAdded:
        //: Sort by date added
        return tr("Date Added");
    case library::TrackSortKey::PlaylistOrder:
        //: Sort by custom playlist order
        return tr("Playlist Order");
    }
    Q_UNREACHABLE_RETURN(QString());
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
