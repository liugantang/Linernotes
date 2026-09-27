// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include <ui/PlaylistListModel.h>

#include <algorithm>

namespace linernotes::ui {

PlaylistListModel::PlaylistListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int PlaylistListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(m_playlists.size());
}

QVariant PlaylistListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount() || index.column() != 0) {
        return { };
    }

    const auto &info = m_playlists.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return info.name;
    case PlaylistIdRole:
        return info.id;
    case IsSmartRole:
        return info.kind == library::PlaylistKind::Smart;
    default:
        return { };
    }
}

QHash<int, QByteArray> PlaylistListModel::roleNames() const
{
    return {
        { PlaylistIdRole, "playlistId" },
        { NameRole, "name" },
        { IsSmartRole, "isSmart" },
    };
}

int PlaylistListModel::count() const
{
    return static_cast<int>(m_playlists.size());
}

void PlaylistListModel::refresh(const QList<library::PlaylistInfo> &playlists)
{
    const int oldCount = static_cast<int>(m_playlists.size());
    beginResetModel();
    m_playlists = playlists;
    endResetModel();
    if (static_cast<int>(m_playlists.size()) != oldCount) {
        emit countChanged(static_cast<int>(m_playlists.size()));
    }
}

QString PlaylistListModel::nameOf(qint64 playlistId) const
{
    const auto it = std::ranges::find_if(
        m_playlists, [playlistId](const auto &p) { return p.id == playlistId; });
    return it != m_playlists.end() ? it->name : QString();
}

bool PlaylistListModel::contains(qint64 playlistId) const
{
    return std::ranges::any_of(
        m_playlists, [playlistId](const auto &p) { return p.id == playlistId; });
}

int PlaylistListModel::indexOf(qint64 playlistId) const
{
    const auto it = std::ranges::find_if(
        m_playlists, [playlistId](const auto &p) { return p.id == playlistId; });
    if (it == m_playlists.end()) {
        return -1;
    }
    return static_cast<int>(std::distance(m_playlists.begin(), it));
}

bool PlaylistListModel::isSmart(qint64 playlistId) const
{
    const auto it = std::ranges::find_if(
        m_playlists, [playlistId](const auto &p) { return p.id == playlistId; });
    return it != m_playlists.end() && it->kind == library::PlaylistKind::Smart;
}

} // namespace linernotes::ui
