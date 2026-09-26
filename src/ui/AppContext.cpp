// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "AppContext.h"

#include "UiLogging.h"

#include <QTimer>

#include <library/CoverStore.h>
#include <library/Database.h>
#include <library/LibraryRoots.h>
#include <library/LibraryWatcher.h>
#include <library/Migrator.h>
#include <library/Scanner.h>
#include <player/PlaybackSnapshot.h>
#include <player/Player.h>
#include <ui/LibraryActions.h>

#include <algorithm>
#include <chrono>
#include <utility>

namespace linernotes::ui {

AppContext::AppContext(Options options, QObject *parent)
    : QObject(parent)
    , m_options(std::move(options))
    , m_player(new player::Player(m_options.playerOptions, this))
    , m_nowPlaying(std::make_unique<NowPlaying>(nullptr, *m_player, this))
    , m_queueModel(std::make_unique<QueueModel>(nullptr, *m_player, this))
{
    connect(this, &AppContext::libraryChanged, m_nowPlaying.get(), &NowPlaying::refresh);
    connect(this, &AppContext::libraryChanged, m_queueModel.get(), &QueueModel::refresh);

    if (!m_options.playbackStatePath.isEmpty()) {
        m_stateStore = std::make_unique<player::PlaybackStateStore>(m_options.playbackStatePath);
        if (const auto snapshot = m_stateStore->load(); snapshot.has_value()) {
            m_player->restore(*snapshot);
        }

        m_saveTimer = new QTimer(this);
        m_saveTimer->setInterval(std::chrono::seconds(30));
        connect(m_saveTimer, &QTimer::timeout, this, [this]() {
            // 仅在播放器处于播放状态时定期保存，避免崩溃/被杀时丢失太多播放进度
            if (m_player != nullptr
                && m_player->state() == player::Player::PlaybackState::Playing) {
                saveState();
            }
        });
        m_saveTimer->start();
    }
}

AppContext::~AppContext()
{
    if (m_scanner) {
        m_scanner->cancel();
    }
    m_nowPlaying.reset();
    m_queueModel.reset();
    m_watcher.reset();
    m_scanner.reset();
    m_actions.reset();
    m_coverStore.reset();
    m_db.reset();
    m_stateStore.reset();
}

void AppContext::saveState() const
{
    if (m_stateStore != nullptr && m_player != nullptr) {
        // 失败时 save 内部已告警
        static_cast<void>(m_stateStore->save(m_player->snapshot()));
    }
}

core::Result<void> AppContext::start()
{
    if (m_libraryReady) {
        return { };
    }

    auto db = std::make_unique<library::Database>(m_options.databasePath);
    const library::Migrator migrator;
    const auto openRes = db->open(migrator);
    if (!openRes.ok()) {
        m_startupError = openRes.error().toString();
        m_libraryReady = false;
        qCCritical(lcUi, "Failed to open and migrate database: %s",
            qPrintable(openRes.error().toString()));
        emit startupErrorChanged();
        emit libraryReadyChanged();
        return openRes.error();
    }

    m_db = std::move(db);
    m_nowPlaying->setDatabase(m_db.get());
    m_queueModel->setDatabase(m_db.get());
    m_coverStore = std::make_unique<library::CoverStore>(m_options.coverCacheDir);
    m_actions = std::make_unique<LibraryActions>(*m_db, *m_player, this);

    library::Scanner::Options scannerOpts;
    scannerOpts.coverStore = m_coverStore.get();
    m_scanner = std::make_unique<library::Scanner>(*m_db, scannerOpts, this);

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
        *m_db, *m_scanner, library::LibraryWatcher::Options { }, this);

    // If there are enabled library roots, start an incremental scan
    const library::LibraryRoots roots(*m_db);
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

    m_watcher->reload();

    m_libraryReady = true;
    m_startupError.clear();
    emit libraryReadyChanged();
    emit startupErrorChanged();

    return { };
}

player::Player *AppContext::player() const
{
    return m_player;
}

NowPlaying *AppContext::nowPlaying() const
{
    return m_nowPlaying.get();
}

QueueModel *AppContext::queueModel() const
{
    return m_queueModel.get();
}

library::Database *AppContext::database()
{
    return m_db.get();
}

library::CoverStore *AppContext::coverStore() const
{
    return m_coverStore.get();
}

LibraryActions *AppContext::actions() const
{
    return m_actions.get();
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

} // namespace linernotes::ui
