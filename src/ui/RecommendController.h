// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtGlobal>

#include <cstdint>

namespace linernotes::core {
class Clock;
class Settings;
} // namespace linernotes::core

namespace linernotes::library {
class Database;
} // namespace linernotes::library

namespace linernotes::rec {
class Recommender;
} // namespace linernotes::rec

namespace linernotes::ui {

class LibraryActions;
class PlaylistController;

class RecommendController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(RecommendController)

public:
    enum class Kind : std::uint8_t { Daily, ForYou };
    Q_ENUM(Kind)

    Q_PROPERTY(
        linernotes::ui::RecommendController::Kind kind READ kind WRITE setKind NOTIFY rowsChanged)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)

    RecommendController(library::Database &db, rec::Recommender &recommender,
        PlaylistController &playlists, LibraryActions &actions, core::Settings &settings,
        const core::Clock &clock, QObject *parent = nullptr);
    ~RecommendController() override = default;

    [[nodiscard]] Kind kind() const;
    void setKind(Kind kind);

    [[nodiscard]] QVariantList rows() const;

    Q_INVOKABLE void load();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void playAll();
    Q_INVOKABLE void playRow(int index);
    Q_INVOKABLE void enqueueAll();
    Q_INVOKABLE qint64 saveAsPlaylist(const QString &name);

signals:
    void rowsChanged();

private:
    void loadDaily();
    bool restoreDaily(const QString &todayStr);
    void loadForYou();
    [[nodiscard]] QList<qint64> collectResultTrackIds() const;

    library::Database &m_db;
    rec::Recommender &m_recommender;
    PlaylistController &m_playlists;
    LibraryActions &m_actions;
    core::Settings &m_settings;
    const core::Clock &m_clock;

    Kind m_kind = Kind::Daily;

    QVariantList m_dailyRows;
    bool m_dailyLoaded = false;
    QString m_dailyDate;

    QVariantList m_forYouRows;
    bool m_forYouLoaded = false;
    quint64 m_forYouSeed = 0;
};

} // namespace linernotes::ui
