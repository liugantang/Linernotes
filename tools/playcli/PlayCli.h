// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QSocketNotifier>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <player/PlayMode.h>
#include <player/PlayQueue.h>
#include <player/PlaybackSnapshot.h>
#include <player/Player.h>

#include <memory>
#include <optional>

namespace linernotes::playcli {

class PlayCli : public QObject {
    Q_OBJECT

public:
    explicit PlayCli(const QString &ao = QString(), QObject *parent = nullptr);
    ~PlayCli() override;

    bool init();
    void setReportMemoryInterval(int intervalSec);
    void setStateFile(const QString &path);

    bool playFiles(const QStringList &files, player::PlayMode mode, std::optional<int> volume);
    bool restoreState(std::optional<player::PlayMode> mode = std::nullopt,
        std::optional<int> volume = std::nullopt);

public slots:
    void quit(int exitCode = 0);

private slots:
    void handleStdin();
    void handleSignal();
    void handleReportMemory();

private:
    void setupSignals();
    void setupStdin();
    void setupPlayer();
    void handleStdinCommand(const QString &line);
    /// 执行一条命令；无法识别或参数无效时返回 false
    bool executeCommand(const QString &trimmed);
    static qint64 readVmRssKb();

    player::Player m_player;
    std::unique_ptr<player::PlaybackStateStore> m_stateStore;
    QString m_stateFilePath;
    QTimer m_reportMemoryTimer;
    QElapsedTimer m_elapsedTimer;
    QSocketNotifier *m_stdinNotifier = nullptr;
    QSocketNotifier *m_signalNotifier = nullptr;
    QByteArray m_stdinBuffer;
    bool m_isQuitting = false;
};

} // namespace linernotes::playcli
