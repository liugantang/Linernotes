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

class TrackListModel : public PagedListModel {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(TrackListModel)

    Q_PROPERTY(SortKey sortKey READ sortKey WRITE setSortKey NOTIFY sortKeyChanged)
    Q_PROPERTY(qint64 albumId READ albumId WRITE setAlbumId NOTIFY albumIdChanged)
    Q_PROPERTY(qint64 artistId READ artistId WRITE setArtistId NOTIFY artistIdChanged)
    Q_PROPERTY(QString genre READ genre WRITE setGenre NOTIFY genreChanged)
    Q_PROPERTY(qint64 playlistId READ playlistId WRITE setPlaylistId NOTIFY playlistIdChanged)

public:
    enum class SortKey : std::uint8_t {
        Default,
        Title,
        Artist,
        Album,
        Year,
        Duration,
        DateAdded,
        PlaylistOrder,
    };
    Q_ENUM(SortKey)

    enum Role : std::uint16_t { // NOLINT(cppcoreguidelines-use-enum-class) - Qt 模型角色需与 int
                                // 互转
        TrackIdRole = Qt::UserRole + 1,
        PathRole,
        TitleRole,
        ArtistRole,
        AlbumRole,
        AlbumArtistRole,
        GenreRole,
        YearRole,
        TrackNumberRole,
        DiscNumberRole,
        DurationMsRole,
        DurationTextRole,
        CodecRole,
        SampleRateRole,
        BitDepthRole,
        BitrateRole,
        CoverHashRole,
        FavoriteRole,
        RatingRole,
        AddedAtRole,
        PlayCountRole,
        LastPlayedAtRole,
    };
    Q_ENUM(Role)

    explicit TrackListModel(QObject *parent = nullptr);
    ~TrackListModel() override = default;

    void setContext(AppContext *context) override;

    // QAbstractListModel interface
    [[nodiscard]] QVariant data(
        const QModelIndex &index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] SortKey sortKey() const;
    void setSortKey(SortKey key);

    [[nodiscard]] qint64 albumId() const;
    void setAlbumId(qint64 albumId);

    [[nodiscard]] qint64 artistId() const;
    void setArtistId(qint64 artistId);

    [[nodiscard]] QString genre() const;
    void setGenre(const QString &genre);

    [[nodiscard]] qint64 playlistId() const;
    void setPlaylistId(qint64 playlistId);

    Q_INVOKABLE qint64 trackIdAt(int row) const;
    Q_INVOKABLE QList<qint64> trackIds(const QList<int> &rows) const;
    Q_INVOKABLE QList<qint64> allTrackIds() const;

signals:
    void sortKeyChanged(SortKey key);
    void albumIdChanged(qint64 albumId);
    void artistIdChanged(qint64 artistId);
    void genreChanged(const QString &genre);
    void playlistIdChanged(qint64 playlistId);

protected:
    core::Result<int> queryCount(const library::LibraryQuery &query) const override;
    void clearCache() override;

private:
    [[nodiscard]] library::TrackFilter currentFilter() const;
    void onPlaylistContentChanged(qint64 id);
    void onPlaylistsChanged();

    SortKey m_sortKey { SortKey::Default };
    qint64 m_albumId { 0 };
    qint64 m_artistId { 0 };
    QString m_genre;
    qint64 m_playlistId { 0 };
    mutable PageCache<library::TrackRow> m_cache { 200, 32 };
};

} // namespace linernotes::ui
