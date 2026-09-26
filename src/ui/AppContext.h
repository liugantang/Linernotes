// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <core/Result.h>
#include <library/CoverStore.h>
#include <library/Database.h>
#include <player/MpvHandle.h>
#include <player/Player.h>
#include <ui/LibraryActions.h>
#include <ui/NowPlaying.h>
#include <ui/PlaylistController.h>
#include <ui/QueueModel.h>
#include <ui/SearchController.h>

#include <memory>

namespace linernotes::library {
class Scanner;
class LibraryWatcher;
} // namespace linernotes::library

namespace linernotes::player {
class PlaybackStateStore;
} // namespace linernotes::player

namespace linernotes::ui {

class AppContext : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(AppContext)

    // QML 从 Q_PROPERTY 拿到的 QObject* 不会被 JS 引擎接管所有权（只有 Q_INVOKABLE
    // 返回值才会），所以值成员是安全的
    Q_PROPERTY(linernotes::player::Player *player READ player CONSTANT)
    Q_PROPERTY(linernotes::ui::NowPlaying *nowPlaying READ nowPlaying CONSTANT)
    Q_PROPERTY(linernotes::ui::QueueModel *queueModel READ queueModel CONSTANT)
    Q_PROPERTY(linernotes::ui::SearchController *search READ search CONSTANT)
    Q_PROPERTY(linernotes::ui::PlaylistController *playlists READ playlists CONSTANT)
    Q_PROPERTY(linernotes::ui::LibraryActions *actions READ actions CONSTANT)
    Q_PROPERTY(bool libraryReady READ isLibraryReady NOTIFY libraryReadyChanged)
    Q_PROPERTY(QString startupError READ startupError NOTIFY startupErrorChanged)
    Q_PROPERTY(bool scanning READ isScanning NOTIFY scanningChanged)
    Q_PROPERTY(QUrl uiStateUrl READ uiStateUrl CONSTANT)

public:
    struct Options {
        QString databasePath;
        QString coverCacheDir;
        player::MpvHandle::OptionList playerOptions;
        // 界面状态（面板折叠等）的 ini 文件。QML 的 Settings 默认依赖 organizationName，而设置它会
        // 改变 QStandardPaths 的目录，所以显式指定文件位置
        QString uiStatePath;
        QString playbackStatePath;
    };

    explicit AppContext(Options options, QObject *parent = nullptr);
    ~AppContext() override;

    /// 打开数据库（执行迁移）、创建扫描器与文件监听；若已有曲库根目录，启动一次增量扫描并开始监听。
    /// 失败时 startupError 为可读的错误信息、libraryReady 为 false，返回错误；播放器仍然可用。
    core::Result<void> start();

    /// 若配置了 playbackStatePath，将当前播放器状态保存至文件。
    void saveState() const;

    [[nodiscard]] player::Player *player();
    [[nodiscard]] NowPlaying *nowPlaying();
    [[nodiscard]] QueueModel *queueModel();
    [[nodiscard]] SearchController *search();
    [[nodiscard]] PlaylistController *playlists();
    [[nodiscard]] library::Database &database();
    [[nodiscard]] const library::Database &database() const;
    [[nodiscard]] library::CoverStore *coverStore();
    [[nodiscard]] LibraryActions *actions();
    [[nodiscard]] bool isLibraryReady() const;
    [[nodiscard]] QString startupError() const;
    [[nodiscard]] bool isScanning() const;
    [[nodiscard]] QUrl uiStateUrl() const;

signals:
    void libraryReadyChanged();
    void startupErrorChanged();
    void scanningChanged();
    void libraryChanged();

private:
    // 声明顺序即依赖顺序，析构逆序进行，依赖方先于被依赖方析构
    Options m_options;
    library::Database m_db;
    player::Player m_player;
    library::CoverStore m_coverStore;
    std::unique_ptr<player::PlaybackStateStore> m_stateStore;
    NowPlaying m_nowPlaying;
    QueueModel m_queueModel;
    SearchController m_search;
    PlaylistController m_playlists;
    LibraryActions m_actions;
    std::unique_ptr<library::Scanner> m_scanner;
    std::unique_ptr<library::LibraryWatcher> m_watcher;
    QTimer m_saveTimer;

    bool m_libraryReady { false };
    QString m_startupError;
    bool m_scanning { false };
};

} // namespace linernotes::ui
