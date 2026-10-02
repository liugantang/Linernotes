// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>

namespace linernotes::core {
class Clock;
class Settings;
} // namespace linernotes::core

namespace linernotes::library {
class Database;
} // namespace linernotes::library

namespace linernotes::player {
class PlayQueue;
} // namespace linernotes::player

namespace linernotes::rec {
class Recommender;
} // namespace linernotes::rec

namespace linernotes::ui {

class EndlessPlay : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(EndlessPlay)
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)

public:
    EndlessPlay(library::Database &db, player::PlayQueue &queue, rec::Recommender &recommender,
        core::Settings &settings, const core::Clock &clock, QObject *parent = nullptr);
    ~EndlessPlay() override = default;

    [[nodiscard]] bool isEnabled() const;
    void setEnabled(bool enabled);

signals:
    void enabledChanged(bool enabled);

private slots:
    void maybeExtend();

private:
    library::Database &m_db;
    player::PlayQueue &m_queue;
    rec::Recommender &m_recommender;
    core::Settings &m_settings;
    const core::Clock &m_clock;

    bool m_inExtend = false;
    int m_lastEmptyCount = -1;
};

} // namespace linernotes::ui
