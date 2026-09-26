// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <library/FsType.h>
#include <library/LibraryRoots.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace linernotes::library {

class Database;
class Scanner;

namespace detail {

/// 纯函数：路径去重与子目录合并：对 {/a/b, /a, /c} 返回 {/a, /c}
QStringList mergeSubDirectories(const QStringList &dirs);

} // namespace detail

/// 监听曲库根目录的变化并触发增量扫描。只在主线程（或创建它的线程）使用。
class LibraryWatcher : public QObject {
    Q_OBJECT
public:
    struct Options {
        int debounceMs = 2000; // 事件合并窗口
        int pollIntervalMs = 15 * 60 * 1000; // 定时增量扫描间隔（网络盘/超限/监听失败的根）
        int maxWatches = -1; // -1 = 读取 /proc/sys/fs/inotify/max_user_watches；测试可注入小值
        std::function<FsKind(const QString &)> fsKind; // 可注入；为空时用 fsKindForPath
    };

    LibraryWatcher(Database &db, Scanner &scanner, Options options, QObject *parent = nullptr);
    ~LibraryWatcher() override = default;

    LibraryWatcher(const LibraryWatcher &) = delete;
    LibraryWatcher &operator=(const LibraryWatcher &) = delete;
    LibraryWatcher(LibraryWatcher &&) = delete;
    LibraryWatcher &operator=(LibraryWatcher &&) = delete;

    /// 读取 enabled 根目录，为每个根选择模式并开始监听。根目录列表变化后再调用一次即可。
    void reload();

    enum class Mode : std::uint8_t { Inotify, Polling };
    Mode modeForRoot(qint64 rootId) const;

signals:
    void rescanRequested(const QStringList &dirs); // 便于测试观察

private slots:
    void onDirectoryChanged(const QString &path);
    void onDebounceTimeout();
    void onScannerFinished();

private:
    struct RootWatchInfo {
        LibraryRoot root;
        Mode mode = Mode::Polling;
        QStringList watchedDirs;
        QTimer *pollTimer = nullptr;
    };

    void clearRoots();
    void initRoot(const LibraryRoot &root, double watchLimit, qint64 maxWatches);
    void initLocalRoot(RootWatchInfo &info, double watchLimit, qint64 maxWatches);
    void removeSuccessfulWatches(const QStringList &dirs, const QStringList &failed);
    void startRootTimer(RootWatchInfo &info);
    void updateSingleInotifyRoot(RootWatchInfo &info, double watchLimit);
    void degradeRootToPolling(
        RootWatchInfo &info, qsizetype removedCount, const QString &warningMsg);

    void flushPendingScan();
    void pollRoot(qint64 rootId);
    qint64 readSystemMaxWatches() const;
    void updateInotifyWatchesAfterScan();

    Database &m_db;
    Scanner &m_scanner;
    Options m_options;

    std::unique_ptr<QFileSystemWatcher> m_watcher;
    QTimer *m_debounceTimer = nullptr;
    QSet<QString> m_pendingDirs;

    QHash<qint64, RootWatchInfo> m_roots;
    qsizetype m_totalInotifyWatches = 0;
};

} // namespace linernotes::library
