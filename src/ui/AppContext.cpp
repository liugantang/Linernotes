// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "AppContext.h"

#include "UiLogging.h"

#include <QDateTime>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>

#include <core/Settings.h>
#include <library/CoverStore.h>
#include <library/Database.h>
#include <library/DatabaseBackup.h>
#include <library/LibraryRoots.h>
#include <library/LibraryWatcher.h>
#include <library/Migrator.h>
#include <library/Scanner.h>
#include <player/PlaybackSnapshot.h>
#include <player/Player.h>
#include <ui/AppSettings.h>
#include <ui/LibraryActions.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace linernotes::ui {

AppContext::AppContext(core::Settings &settings, Options options, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_options(std::move(options))
    , m_db(m_options.databasePath)
    , m_playStats(m_db)
    , m_tagEditor(m_db)
    , m_roots(m_db)
    , m_marks(m_db)
    , m_player(m_options.playerOptions)
    , m_recorder(m_player, m_db, m_clock)
    , m_settingsController(m_settings, m_player)
    , m_coverStore(m_options.coverCacheDir)
    , m_nowPlaying(m_db, m_player, m_coverStore)
    , m_queueModel(m_db, m_player)
    , m_search(m_db)
    , m_playlists(m_db, m_player)
    , m_actions(m_db, m_player)
{
    connect(this, &AppContext::libraryChanged, &m_nowPlaying, &NowPlaying::refresh);
    connect(&m_marks, &MarksController::marksChanged, &m_nowPlaying, &NowPlaying::refresh);
    connect(&m_tagEditor, &TagEditorModel::saved, this, &AppContext::libraryChanged);
    connect(this, &AppContext::libraryChanged, &m_queueModel, &QueueModel::refresh);
    connect(this, &AppContext::libraryChanged, &m_search, &SearchController::refresh);
    connect(this, &AppContext::libraryChanged, &m_playlists, &PlaylistController::refresh);
    connect(&m_recorder, &PlayEventRecorder::playEventFinished, this, [this](qint64 trackId) {
        if (m_libraryReady) {
            const auto res
                = m_playStats.refreshTrack(trackId, m_settingsController.playCountRule());
            if (!res.ok()) {
                qCWarning(lcUi, "Failed to refresh play stats for track %lld: %s", trackId,
                    qPrintable(res.error().toString()));
            } else {
                emit playStatsChanged();
            }
        }
    });
    connect(&m_settingsController, &SettingsController::playCountRuleChanged, this, [this]() {
        if (m_libraryReady) {
            const auto res = m_playStats.rebuildAll(m_settingsController.playCountRule());
            if (!res.ok()) {
                qCWarning(lcUi, "Failed to rebuild play stats on rule change: %s",
                    qPrintable(res.error().toString()));
            } else {
                emit playStatsChanged();
            }
        }
    });
    connect(&m_roots, &LibraryRootsModel::rootsChanged, this, [this]() {
        if (m_watcher) {
            m_watcher->reload();
        }
        rescan();
    });

    if (!m_options.playbackStatePath.isEmpty()) {
        m_stateStore = std::make_unique<player::PlaybackStateStore>(m_options.playbackStatePath);
        if (const auto snapshot = m_stateStore->load(); snapshot.has_value()) {
            m_player.restore(*snapshot);
        }

        m_saveTimer.setInterval(std::chrono::seconds(30));
        connect(&m_saveTimer, &QTimer::timeout, this, [this]() {
            // 仅在播放器处于播放状态时定期保存，避免崩溃/被杀时丢失太多播放进度
            if (m_player.state() == player::Player::PlaybackState::Playing) {
                saveState();
            }
        });
        m_saveTimer.start();
    }

    m_backupTimer.setInterval(std::chrono::hours(1));
    connect(&m_backupTimer, &QTimer::timeout, this, &AppContext::triggerBackupIfDue);
    connect(&m_backupWatcher, &QFutureWatcher<core::Result<QString>>::finished, this, [this]() {
        const auto res = m_backupWatcher.result();
        if (res.ok()) {
            qCInfo(lcUi, "Database backup completed successfully: %s", qPrintable(res.value()));
        } else {
            qCWarning(lcUi, "Database backup failed: %s", qPrintable(res.error().toString()));
        }
    });
}

AppContext::~AppContext()
{
    m_backupTimer.stop();
    if (m_backupFuture.isRunning()) {
        m_backupFuture.waitForFinished();
    }
    if (m_scanner) {
        m_scanner->cancel();
    }
}

void AppContext::saveState() const
{
    if (m_stateStore != nullptr) {
        // 失败时 save 内部已告警
        static_cast<void>(m_stateStore->save(m_player.snapshot()));
    }
}

core::Result<void> AppContext::start()
{
    if (m_libraryReady) {
        return { };
    }

    const library::Migrator migrator;
    const auto openRes = m_db.open(migrator);
    if (!openRes.ok()) {
        m_startupError = openRes.error().toString();
        m_libraryReady = false;
        qCCritical(lcUi, "Failed to open and migrate database: %s",
            qPrintable(openRes.error().toString()));
        emit startupErrorChanged();
        emit libraryReadyChanged();
        return openRes.error();
    }

    const auto rebuildRes = m_playStats.rebuildAll(m_settingsController.playCountRule());
    if (!rebuildRes.ok()) {
        qCWarning(lcUi, "Failed to rebuild play stats on startup: %s",
            qPrintable(rebuildRes.error().toString()));
    }

    m_nowPlaying.refresh();
    m_queueModel.refresh();
    m_search.refresh();
    m_playlists.refresh();
    m_roots.refresh();

    library::Scanner::Options scannerOpts;
    scannerOpts.coverStore = &m_coverStore;
    m_scanner = std::make_unique<library::Scanner>(m_db, scannerOpts);

    connect(m_scanner.get(), &library::Scanner::finished, this,
        [this](const library::ScanStats &stats) {
            if (m_scanning) {
                m_scanning = false;
                emit scanningChanged();
            }
            if (!stats.cancelled) {
                // Emit libraryChanged when library content has additions, modifications,
                // removals, moves, missing/restored files, or orphaned entity cleanups.
                const bool hasChanges = stats.added > 0 || stats.updated > 0 || stats.moved > 0
                    || stats.missing > 0 || stats.restored > 0 || stats.albumsRemoved > 0
                    || stats.artistsRemoved > 0;
                if (hasChanges) {
                    emit libraryChanged();
                }
            }
        });

    connect(
        m_scanner.get(), &library::Scanner::progress, this, [this](const library::ScanProgress &) {
            if (!m_scanning) {
                m_scanning = true;
                emit scanningChanged();
            }
        });

    m_watcher = std::make_unique<library::LibraryWatcher>(
        m_db, *m_scanner, library::LibraryWatcher::Options { });

    m_libraryReady = true;
    m_startupError.clear();
    emit libraryReadyChanged();
    emit startupErrorChanged();

    m_watcher->reload();
    rescan();

    if (!m_options.backupDir.isEmpty()) {
        triggerBackupIfDue();
        m_backupTimer.start();
    }

    return { };
}

void AppContext::rescan()
{
    if (!m_libraryReady || !m_scanner) {
        return;
    }
    if (!m_scanning) {
        const library::LibraryRoots roots(m_db);
        const auto rootsListRes = roots.list();
        if (rootsListRes.ok()) {
            const auto &rootsList = rootsListRes.value();
            const bool hasEnabledRoot = std::ranges::any_of(
                rootsList, [](const library::LibraryRoot &r) { return r.enabled; });
            if (hasEnabledRoot) {
                if (m_scanner->start()) {
                    m_scanning = true;
                    emit scanningChanged();
                }
            }
        }
    }
}

player::Player *AppContext::player()
{
    return &m_player;
}

NowPlaying *AppContext::nowPlaying()
{
    return &m_nowPlaying;
}

QueueModel *AppContext::queueModel()
{
    return &m_queueModel;
}

SearchController *AppContext::search()
{
    return &m_search;
}

PlaylistController *AppContext::playlists()
{
    return &m_playlists;
}

MarksController *AppContext::marks()
{
    return &m_marks;
}

SettingsController *AppContext::settings()
{
    return &m_settingsController;
}

LibraryRootsModel *AppContext::libraryRoots()
{
    return &m_roots;
}

TagEditorModel *AppContext::tagEditor()
{
    return &m_tagEditor;
}

library::Database &AppContext::database()
{
    return m_db;
}

const library::Database &AppContext::database() const
{
    return m_db;
}

library::CoverStore *AppContext::coverStore()
{
    return &m_coverStore;
}

LibraryActions *AppContext::actions()
{
    return &m_actions;
}

bool AppContext::isLibraryReady() const
{
    return m_libraryReady;
}

QString AppContext::startupError() const
{
    return m_startupError;
}

bool AppContext::isScanning() const
{
    return m_scanning;
}

QUrl AppContext::uiStateUrl() const
{
    return QUrl::fromLocalFile(m_options.uiStatePath);
}

void AppContext::triggerBackupIfDue()
{
    if (m_options.backupDir.isEmpty()) {
        return;
    }
    if (m_backupFuture.isRunning()) {
        return;
    }

    const int keep = m_settings.value(kLibraryBackupKeep);
    const library::DatabaseBackup::Options opts {
        .backupDir = m_options.backupDir,
        .keep = keep,
    };
    const library::DatabaseBackup backup(m_db, opts);
    const QDateTime now = QDateTime::currentDateTime();
    if (!backup.isDue(now)) {
        return;
    }

    m_backupFuture = QtConcurrent::run([opts, &db = m_db, now]() -> core::Result<QString> {
        library::DatabaseBackup workerBackup(db, opts);
        return workerBackup.backupNow(now);
    });
    m_backupWatcher.setFuture(m_backupFuture);
}

} // namespace linernotes::ui
