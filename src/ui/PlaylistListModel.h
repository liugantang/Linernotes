// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>

#include <library/PlaylistStore.h>

#include <cstdint>

namespace linernotes::ui {

class PlaylistListModel : public QAbstractListModel {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(PlaylistListModel)

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role : std::uint16_t { // NOLINT(cppcoreguidelines-use-enum-class) - Qt 模型角色需与 int
                                // 互转
        PlaylistIdRole = Qt::UserRole + 1,
        NameRole,
        IsSmartRole,
    };
    Q_ENUM(Role)

    explicit PlaylistListModel(QObject *parent = nullptr);
    ~PlaylistListModel() override = default;

    [[nodiscard]] int rowCount(const QModelIndex &parent = { }) const override;
    [[nodiscard]] QVariant data(
        const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const;
    void refresh(const QList<library::PlaylistInfo> &playlists);

    [[nodiscard]] Q_INVOKABLE QString nameOf(qint64 playlistId) const;
    [[nodiscard]] Q_INVOKABLE bool contains(qint64 playlistId) const;
    [[nodiscard]] Q_INVOKABLE int indexOf(qint64 playlistId) const;
    [[nodiscard]] Q_INVOKABLE bool isSmart(qint64 playlistId) const;

signals:
    void countChanged(int count);

private:
    QList<library::PlaylistInfo> m_playlists;
};

} // namespace linernotes::ui
