// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QFuture>
#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include <butler/DuplicateResolver.h>
#include <core/Clock.h>
#include <core/Result.h>
#include <library/CoverStore.h>
#include <library/Database.h>
#include <library/PlayStats.h>
#include <library/Scanner.h>
#include <player/MpvHandle.h>
#include <player/Player.h>
#include <ui/AiContext.h>
#include <ui/AiSettingsController.h>
#include <ui/CleanupController.h>
#include <ui/CorrectionReviewController.h>
#include <ui/CoverSearchController.h>
#include <ui/DuplicateController.h>
#include <ui/LibraryActions.h>
#include <ui/LibraryRootsModel.h>
#include <ui/LlmDebugController.h>
#include <ui/MarksController.h>
#include <ui/NowPlaying.h>
#include <ui/PlayEventRecorder.h>
#include <ui/PlaylistController.h>
#include <ui/QueueModel.h>
#include <ui/SearchController.h>
#include <ui/SettingsController.h>
#include <ui/TagEditorModel.h>
#include <ui/WritebackController.h>

#include <memory>

namespace linernotes::core {
class Settings;
} // namespace linernotes::core

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
    Q_PROPERTY(linernotes::ui::MarksController *marks READ marks CONSTANT)
    Q_PROPERTY(linernotes::ui::LibraryActions *actions READ actions CONSTANT)
    Q_PROPERTY(linernotes::ui::SettingsController *settings READ settings CONSTANT)
    Q_PROPERTY(linernotes::ui::AiSettingsController *aiSettings READ aiSettings CONSTANT)
    Q_PROPERTY(linernotes::ui::LlmDebugController *llmDebug READ llmDebug CONSTANT)
    Q_PROPERTY(linernotes::ui::LibraryRootsModel *libraryRoots READ libraryRoots CONSTANT)
    Q_PROPERTY(linernotes::ui::TagEditorModel *tagEditor READ tagEditor CONSTANT)
    Q_PROPERTY(linernotes::ui::CorrectionReviewController *review READ review CONSTANT)
    Q_PROPERTY(linernotes::ui::WritebackController *writeback READ writeback CONSTANT)
    Q_PROPERTY(linernotes::ui::CleanupController *cleanup READ cleanup CONSTANT)
    Q_PROPERTY(linernotes::ui::DuplicateController *duplicates READ duplicates CONSTANT)
    Q_PROPERTY(linernotes::ui::CoverSearchController *coverSearch READ coverSearch CONSTANT)
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
        QString backupDir;
        QString promptsDir { };
    };

    explicit AppContext(core::Settings &settings, Options options, QObject *parent = nullptr);
    ~AppContext() override;

    /// 打开数据库（执行迁移）、创建扫描器与文件监听；若已有曲库根目录，启动一次增量扫描并开始监听。
    /// 失败时 startupError 为可读的错误信息、libraryReady 为 false，返回错误；播放器仍然可用。
    core::Result<void> start();

    /// 若配置了 playbackStatePath，将当前播放器状态保存至文件。
    void saveState() const;

    /// 重新扫描曲库（库就绪且未在扫描时启动）。
    Q_INVOKABLE void rescan();

    [[nodiscard]] player::Player *player();
    [[nodiscard]] NowPlaying *nowPlaying();
    [[nodiscard]] QueueModel *queueModel();
    [[nodiscard]] SearchController *search();
    [[nodiscard]] PlaylistController *playlists();
    [[nodiscard]] MarksController *marks();
    [[nodiscard]] SettingsController *settings();
    [[nodiscard]] AiSettingsController *aiSettings();
    [[nodiscard]] LlmDebugController *llmDebug();
    [[nodiscard]] LibraryRootsModel *libraryRoots();
    [[nodiscard]] TagEditorModel *tagEditor();
    [[nodiscard]] CorrectionReviewController *review();
    [[nodiscard]] WritebackController *writeback();
    [[nodiscard]] CleanupController *cleanup();
    [[nodiscard]] DuplicateController *duplicates();
    [[nodiscard]] CoverSearchController *coverSearch();
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
    void playStatsChanged();

private:
    void triggerBackupIfDue();
    void connectScannerSignals();

    // 声明顺序即依赖顺序，析构逆序进行，依赖方先于被依赖方析构
    core::Settings &m_settings;
    Options m_options;
    core::SystemClock m_clock;
    library::Database m_db;
    library::CoverStore m_coverStore;
    library::Scanner m_scanner;
    AiContext m_ai;
    WritebackController m_writeback;
    CleanupController m_cleanup;
    butler::SystemFileTrash m_trash;
    DuplicateController m_duplicates;
    CoverSearchController m_coverSearch;
    library::PlayStats m_playStats;
    TagEditorModel m_tagEditor;
    CorrectionReviewController m_review;
    LibraryRootsModel m_roots;
    MarksController m_marks;
    player::Player m_player;
    PlayEventRecorder m_recorder;
    SettingsController m_settingsController;
    std::unique_ptr<player::PlaybackStateStore> m_stateStore;
    NowPlaying m_nowPlaying;
    QueueModel m_queueModel;
    SearchController m_search;
    PlaylistController m_playlists;
    LibraryActions m_actions;
    std::unique_ptr<library::LibraryWatcher> m_watcher;
    QTimer m_saveTimer;
    QTimer m_backupTimer;
    QFutureWatcher<core::Result<QString>> m_backupWatcher;
    QFuture<core::Result<QString>> m_backupFuture;

    bool m_libraryReady { false };
    QString m_startupError;
    bool m_scanning { false };
};

} // namespace linernotes::ui
