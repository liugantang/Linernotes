// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QString>

#include <library/LibraryQuery.h>
#include <ui/PageCache.h>
#include <ui/PagedListModel.h>

#include <cstdint>

namespace linernotes::ui {

class AlbumGridModel : public PagedListModel {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(AlbumGridModel)

    Q_PROPERTY(SortKey sortKey READ sortKey WRITE setSortKey NOTIFY sortKeyChanged)
    Q_PROPERTY(qint64 artistId READ artistId WRITE setArtistId NOTIFY artistIdChanged)

public:
    enum class SortKey : std::uint8_t {
        Title,
        Artist,
        Year,
        DateAdded,
    };
    Q_ENUM(SortKey)

    enum Role : std::uint16_t { // NOLINT(cppcoreguidelines-use-enum-class) - Qt 模型角色需与 int
                                // 互转
        AlbumIdRole = Qt::UserRole + 1,
        TitleRole,
        AlbumArtistRole,
        YearRole,
        TrackCountRole,
        DurationMsRole,
        DurationTextRole,
        CoverHashRole,
        FavoriteRole,
    };
    Q_ENUM(Role)

    explicit AlbumGridModel(QObject *parent = nullptr);
    ~AlbumGridModel() override = default;

    // QAbstractListModel interface
    [[nodiscard]] QVariant data(
        const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] SortKey sortKey() const;
    void setSortKey(SortKey key);

    [[nodiscard]] qint64 artistId() const;
    void setArtistId(qint64 artistId);

signals:
    void sortKeyChanged(SortKey key);
    void artistIdChanged(qint64 artistId);

protected:
    core::Result<int> queryCount(const library::LibraryQuery &query) const override;
    void clearCache() override;

private:
    [[nodiscard]] library::AlbumFilter currentFilter() const;

    SortKey m_sortKey { SortKey::Title };
    qint64 m_artistId { 0 };
    mutable PageCache<library::AlbumRow> m_cache { 200, 32 };
};

} // namespace linernotes::ui
