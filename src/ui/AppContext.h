// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <core/Result.h>
#include <player/MpvHandle.h>
#include <player/Player.h>
#include <ui/LibraryActions.h>
#include <ui/NowPlaying.h>
#include <ui/QueueModel.h>
#include <ui/SearchController.h>

#include <memory>

class QTimer;

namespace linernotes::library {
class Database;
class CoverStore;
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

    Q_PROPERTY(linernotes::player::Player *player READ player CONSTANT)
    Q_PROPERTY(linernotes::ui::NowPlaying *nowPlaying READ nowPlaying CONSTANT)
    Q_PROPERTY(linernotes::ui::QueueModel *queueModel READ queueModel CONSTANT)
    Q_PROPERTY(linernotes::ui::SearchController *search READ search CONSTANT)
    Q_PROPERTY(linernotes::ui::LibraryActions *actions READ actions NOTIFY libraryReadyChanged)
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

    [[nodiscard]] player::Player *player() const;
    [[nodiscard]] NowPlaying *nowPlaying() const;
    [[nodiscard]] QueueModel *queueModel() const;
    [[nodiscard]] SearchController *search() const;
    [[nodiscard]] library::Database *database();
    [[nodiscard]] library::CoverStore *coverStore() const;
    [[nodiscard]] LibraryActions *actions() const;
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
    Options m_options;
    player::Player *m_player { nullptr };
    std::unique_ptr<player::PlaybackStateStore> m_stateStore;
    QTimer *m_saveTimer { nullptr };
    std::unique_ptr<NowPlaying> m_nowPlaying;
    std::unique_ptr<QueueModel> m_queueModel;
    std::unique_ptr<SearchController> m_search;
    std::unique_ptr<library::Database> m_db;
    std::unique_ptr<library::CoverStore> m_coverStore;
    std::unique_ptr<LibraryActions> m_actions;
    std::unique_ptr<library::Scanner> m_scanner;
    std::unique_ptr<library::LibraryWatcher> m_watcher;

    bool m_libraryReady { false };
    QString m_startupError;
    bool m_scanning { false };
};

} // namespace linernotes::ui
