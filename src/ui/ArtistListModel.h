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

class ArtistListModel : public PagedListModel {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ArtistListModel)

    Q_PROPERTY(SortKey sortKey READ sortKey WRITE setSortKey NOTIFY sortKeyChanged)

public:
    enum class SortKey : std::uint8_t {
        Name,
    };
    Q_ENUM(SortKey)

    enum Role : std::uint16_t { // NOLINT(cppcoreguidelines-use-enum-class) - Qt 模型角色需与 int
                                // 互转
        ArtistIdRole = Qt::UserRole + 1,
        NameRole,
        TrackCountRole,
        AlbumCountRole,
        CoverHashRole,
        FavoriteRole,
    };
    Q_ENUM(Role)

    explicit ArtistListModel(QObject *parent = nullptr);
    ~ArtistListModel() override = default;

    // QAbstractListModel interface
    [[nodiscard]] QVariant data(
        const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] SortKey sortKey() const;
    void setSortKey(SortKey key);

signals:
    void sortKeyChanged(SortKey key);

protected:
    core::Result<int> queryCount(const library::LibraryQuery &query) const override;
    void clearCache() override;

private:
    [[nodiscard]] library::ArtistFilter currentFilter() const;

    SortKey m_sortKey { SortKey::Name };
    mutable PageCache<library::ArtistRow> m_cache { 200, 32 };
};

} // namespace linernotes::ui
