// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>
#include <QString>

#include <core/Result.h>

namespace linernotes::library {
class CoverStore;
} // namespace linernotes::library

namespace linernotes::player {
class Player;
} // namespace linernotes::player

namespace linernotes::ui {

class NowPlaying;

class Mpris : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Mpris)

public:
    Mpris(player::Player &player, NowPlaying &nowPlaying, const library::CoverStore &covers,
        QObject *parent = nullptr);
    ~Mpris() override;

    /// 在会话总线注册服务 org.mpris.MediaPlayer2.linernotes 与对象 /org/mpris/MediaPlayer2。
    /// 服务名已被占用时改用 org.mpris.MediaPlayer2.linernotes.instance<pid>（规范允许）。
    /// 无会话总线或注册失败 → 返回错误（调用方只告警，不影响启动）。
    core::Result<void> registerOnBus();

signals:
    void raiseRequested();

private:
    player::Player &m_player;
    NowPlaying &m_nowPlaying;
    const library::CoverStore &m_covers;
    bool m_registered = false;
    QString m_serviceName;
};

} // namespace linernotes::ui
