// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "LibraryWatcher.h"

#include <QDir>
#include <QFile>

#include <library/Database.h>
#include <library/DirectoryWalker.h>
#include <library/LibraryLogging.h>
#include <library/Scanner.h>

#include <algorithm>

namespace linernotes::library {

namespace detail {

QStringList mergeSubDirectories(const QStringList &dirs)
{
    if (dirs.isEmpty()) {
        return { };
    }

    QSet<QString> uniquePaths;
    for (const auto &d : dirs) {
        if (!d.isEmpty()) {
            uniquePaths.insert(QDir::cleanPath(d));
        }
    }

    QStringList sortedList = uniquePaths.values();
    std::ranges::sort(sortedList, [](const QString &a, const QString &b) {
        if (a.length() != b.length()) {
            return a.length() < b.length();
        }
        return a < b;
    });

    QStringList result;
    for (const auto &path : sortedList) {
        bool isSub = false;
        for (const auto &parent : result) {
            if (parent == QStringLiteral("/")) {
                if (path != QStringLiteral("/")) {
                    isSub = true;
                    break;
                }
            } else {
                if (path.startsWith(parent + u'/')) {
                    isSub = true;
                    break;
                }
            }
        }
        if (!isSub) {
            result.append(path);
        }
    }

    std::ranges::sort(result);
    return result;
}

} // namespace detail

LibraryWatcher::LibraryWatcher(Database &db, Scanner &scanner, Options options, QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_scanner(scanner)
    , m_options(std::move(options))
    , m_debounceTimer(new QTimer(this))
{
    m_debounceTimer->setSingleShot(true);
    connect(m_debounceTimer, &QTimer::timeout, this, &LibraryWatcher::onDebounceTimeout);
    connect(&m_scanner, &Scanner::finished, this, &LibraryWatcher::onScannerFinished);

    reload();
}

void LibraryWatcher::clearRoots()
{
    for (auto &info : m_roots) {
        if (info.pollTimer != nullptr) {
            info.pollTimer->stop();
            info.pollTimer->deleteLater();
            info.pollTimer = nullptr;
        }
    }
    m_roots.clear();
    m_totalInotifyWatches = 0;
}

void LibraryWatcher::reload()
{
    clearRoots();

    m_watcher = std::make_unique<QFileSystemWatcher>(this);
    connect(m_watcher.get(), &QFileSystemWatcher::directoryChanged, this,
        &LibraryWatcher::onDirectoryChanged);

    const auto rootsRes = LibraryRoots(m_db).list();
    if (!rootsRes.ok()) {
        qCWarning(lcLibrary) << "Failed to list library roots during watcher reload:"
                             << rootsRes.error().toString();
        return;
    }

    const qint64 maxWatches = readSystemMaxWatches();
    const double watchLimit = static_cast<double>(maxWatches) * 0.8;

    for (const auto &root : rootsRes.value()) {
        if (!root.enabled) {
            continue;
        }
        initRoot(root, watchLimit, maxWatches);
    }
}

void LibraryWatcher::initRoot(const LibraryRoot &root, double watchLimit, qint64 maxWatches)
{
    FsKind kind = FsKind::Unknown;
    if (m_options.fsKind) {
        kind = m_options.fsKind(root.path);
    } else {
        kind = fsKindForPath(root.path);
    }

    RootWatchInfo info;
    info.root = root;

    if (kind == FsKind::Local) {
        initLocalRoot(info, watchLimit, maxWatches);
    } else {
        info.mode = Mode::Polling;
    }

    startRootTimer(info);
    m_roots.insert(root.id, info);
}

void LibraryWatcher::initLocalRoot(RootWatchInfo &info, double watchLimit, qint64 maxWatches)
{
    const QStringList dirs = DirectoryWalker::listDirectories(info.root.path, info.root.excludes);
    if (dirs.isEmpty()) {
        info.mode = Mode::Polling;
        return;
    }

    const auto totalAfterAdd = static_cast<double>(m_totalInotifyWatches + dirs.size());
    if (totalAfterAdd > watchLimit) {
        qCWarning(lcLibrary)
            << "Inotify watch limit reached for root" << info.root.path
            << "(current watches:" << m_totalInotifyWatches << "+ root dirs:" << dirs.size()
            << "> limit:" << watchLimit << "of max:" << maxWatches
            << "), falling back to polling. Consider increasing fs.inotify.max_user_watches.";
        info.mode = Mode::Polling;
        return;
    }

    const QStringList failed = m_watcher->addPaths(dirs);
    const double failRate = static_cast<double>(failed.size()) / static_cast<double>(dirs.size());
    if (failRate > 0.01) {
        qCWarning(lcLibrary) << "Failed to add inotify watches for root" << info.root.path
                             << "(failed" << failed.size() << "of" << dirs.size()
                             << "dirs), falling back to polling.";
        removeSuccessfulWatches(dirs, failed);
        info.mode = Mode::Polling;
        return;
    }

    info.mode = Mode::Inotify;
    info.watchedDirs = dirs;
    m_totalInotifyWatches += (dirs.size() - failed.size());
}

void LibraryWatcher::removeSuccessfulWatches(const QStringList &dirs, const QStringList &failed)
{
    if (failed.size() >= dirs.size()) {
        return;
    }
    QStringList successful;
    const QSet<QString> failedSet(failed.cbegin(), failed.cend());
    for (const auto &d : dirs) {
        if (!failedSet.contains(d)) {
            successful.append(d);
        }
    }
    if (!successful.isEmpty()) {
        m_watcher->removePaths(successful);
    }
}

void LibraryWatcher::startRootTimer(RootWatchInfo &info)
{
    auto *timer = new QTimer(this);
    const qint64 rootId = info.root.id;
    connect(timer, &QTimer::timeout, this, [this, rootId]() { pollRoot(rootId); });

    if (info.mode == Mode::Polling) {
        timer->start(std::max(1, m_options.pollIntervalMs));
    } else {
        // Note: 文件修改（内容变化但目录项不变）inotify 的目录监听收不到，
        // 设置较长的兜底定时增量扫描（pollIntervalMs * 4）以弥补此局限。
        timer->start(std::max(1, m_options.pollIntervalMs * 4));
    }
    info.pollTimer = timer;
}

LibraryWatcher::Mode LibraryWatcher::modeForRoot(qint64 rootId) const
{
    const auto it = m_roots.constFind(rootId);
    if (it != m_roots.constEnd()) {
        return it.value().mode;
    }
    return Mode::Polling;
}

void LibraryWatcher::onDirectoryChanged(const QString &path)
{
    m_pendingDirs.insert(QDir::cleanPath(path));
    m_debounceTimer->start(std::max(0, m_options.debounceMs));
}

void LibraryWatcher::onDebounceTimeout()
{
    flushPendingScan();
}

void LibraryWatcher::onScannerFinished()
{
    updateInotifyWatchesAfterScan();

    if (!m_pendingDirs.isEmpty() && !m_debounceTimer->isActive()) {
        flushPendingScan();
    }
}

void LibraryWatcher::pollRoot(qint64 rootId)
{
    const auto it = m_roots.constFind(rootId);
    if (it == m_roots.constEnd()) {
        return;
    }
    m_pendingDirs.insert(it.value().root.path);
    flushPendingScan();
}

void LibraryWatcher::flushPendingScan()
{
    if (m_pendingDirs.isEmpty()) {
        return;
    }

    if (m_scanner.isRunning()) {
        return;
    }

    const QStringList dirsToScan = detail::mergeSubDirectories(m_pendingDirs.values());
    emit rescanRequested(dirsToScan);

    const bool started = m_scanner.startPaths(dirsToScan);
    if (started) {
        m_pendingDirs.clear();
        if (m_debounceTimer->isActive()) {
            m_debounceTimer->stop();
        }
    }
}

qint64 LibraryWatcher::readSystemMaxWatches() const
{
    if (m_options.maxWatches >= 0) {
        return m_options.maxWatches;
    }
#ifdef Q_OS_LINUX
    QFile f(QStringLiteral("/proc/sys/fs/inotify/max_user_watches"));
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        bool ok = false;
        const qint64 val = f.readAll().trimmed().toLongLong(&ok);
        if (ok && val > 0) {
            return val;
        }
    }
#endif
    return 8192;
}

void LibraryWatcher::updateInotifyWatchesAfterScan()
{
    const qint64 maxWatches = readSystemMaxWatches();
    const double watchLimit = static_cast<double>(maxWatches) * 0.8;

    for (auto &info : m_roots) {
        if (info.mode == Mode::Inotify) {
            updateSingleInotifyRoot(info, watchLimit);
        }
    }
}

void LibraryWatcher::updateSingleInotifyRoot(RootWatchInfo &info, double watchLimit)
{
    const QStringList newDirs
        = DirectoryWalker::listDirectories(info.root.path, info.root.excludes);
    const QSet<QString> oldDirSet(info.watchedDirs.cbegin(), info.watchedDirs.cend());
    const QSet<QString> newDirSet(newDirs.cbegin(), newDirs.cend());

    QStringList toRemove;
    for (const auto &d : info.watchedDirs) {
        if (!newDirSet.contains(d)) {
            toRemove.append(d);
        }
    }

    QStringList toAdd;
    for (const auto &d : newDirs) {
        if (!oldDirSet.contains(d)) {
            toAdd.append(d);
        }
    }

    if (!toRemove.isEmpty()) {
        m_watcher->removePaths(toRemove);
        m_totalInotifyWatches -= toRemove.size();
    }

    if (toAdd.isEmpty()) {
        info.watchedDirs = newDirs;
        return;
    }

    const auto totalAfterAdd = static_cast<double>(m_totalInotifyWatches + toAdd.size());
    if (totalAfterAdd > watchLimit) {
        degradeRootToPolling(info, toRemove.size(),
            QStringLiteral("Inotify watch limit reached for root %1, falling back to polling. "
                           "Consider increasing fs.inotify.max_user_watches.")
                .arg(info.root.path));
        return;
    }

    const QStringList failed = m_watcher->addPaths(toAdd);
    const double failRate = newDirs.isEmpty()
        ? 0.0
        : static_cast<double>(failed.size()) / static_cast<double>(newDirs.size());
    if (failRate > 0.01) {
        degradeRootToPolling(info, toRemove.size(),
            QStringLiteral(
                "Too many directories failed to watch for root %1, falling back to polling.")
                .arg(info.root.path));
        return;
    }

    m_totalInotifyWatches += (toAdd.size() - failed.size());
    info.watchedDirs = newDirs;
}

void LibraryWatcher::degradeRootToPolling(
    RootWatchInfo &info, qsizetype removedCount, const QString &warningMsg)
{
    qCWarning(lcLibrary) << warningMsg;
    m_watcher->removePaths(info.watchedDirs);
    m_totalInotifyWatches -= (info.watchedDirs.size() - removedCount);
    info.watchedDirs.clear();
    info.mode = Mode::Polling;
    if (info.pollTimer != nullptr) {
        info.pollTimer->start(std::max(1, m_options.pollIntervalMs));
    }
}

} // namespace linernotes::library
