// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <core/Result.h>

#include <atomic>
#include <cstdint>
#include <functional>

namespace linernotes::library {

class Database;

struct ScanStats {
    int found = 0; // 遍历到的音频文件数
    int added = 0; // 新文件
    int updated = 0; // size 或 mtime 变化而重新读取
    int unchanged = 0;
    int moved = 0; // 通过指纹匹配到“缺失”文件，视为移动
    int missing = 0; // 本次新标记为缺失
    int restored = 0; // 之前缺失、本次在原路径重新出现
    int failed = 0; // 读取失败（仍写入 files 行并记录 scan_error）
    int albumsRemoved = 0; // 清理的孤立专辑数
    int artistsRemoved = 0; // 清理的孤立艺人数
    bool cancelled = false;
    qint64 elapsedMs = 0;
    QString error;

    bool operator==(const ScanStats &other) const = default;
};

struct ScanProgress {
    enum class Phase : std::uint8_t { Walking, Reading, Finishing };
    Phase phase { Phase::Walking };
    int done = 0;
    int total = 0;

    bool operator==(const ScanProgress &other) const = default;
};

/// 扫描器。一次扫描在内部的专用线程上运行，该线程同时是**唯一的写线程**；
/// 标签读取与指纹计算放到 QThreadPool（默认 idealThreadCount）。
/// 线程安全：start/cancel 在创建 Scanner 的线程调用；信号从扫描线程发出（使用者用 QueuedConnection
/// 接收）。
class Scanner : public QObject {
    Q_OBJECT
public:
    struct Options {
        int batchSize = 500; // 每个写事务的文件数
        int maxThreads = 0; // 0 = QThread::idealThreadCount()
        std::function<qint64()> nowMs; // 可注入时钟；为空时用当前 UTC 毫秒
    };

    Scanner(Database &db, Options options, QObject *parent = nullptr);
    ~Scanner() override; // 若在扫描中：cancel 并等待结束

    Scanner(const Scanner &) = delete;
    Scanner &operator=(const Scanner &) = delete;
    Scanner(Scanner &&) = delete;
    Scanner &operator=(Scanner &&) = delete;

    /// 扫描全部 enabled 根目录。正在扫描时返回 false。
    bool start();
    /// 只扫描指定目录（必须位于某个根目录内；供 2.8
    /// 文件监听使用）；缺失检测只在这些目录范围内进行。
    bool startPaths(const QStringList &dirs);
    void cancel();
    bool isRunning() const;
    /// 阻塞版本：在调用线程同步完成一次扫描（CLI 与测试使用）。内部逻辑与异步版本共用。
    core::Result<ScanStats> scanBlocking(const QStringList &dirs = { });

signals:
    void progress(const linernotes::library::ScanProgress &progress);
    void finished(const linernotes::library::ScanStats &stats);

private:
    core::Result<ScanStats> doScan(const QStringList &dirs);
    qint64 getNow() const;

    Database &m_db;
    Options m_options;
    std::atomic<bool> m_running { false };
    std::atomic<bool> m_cancelled { false };
    class QThread *m_workerThread { nullptr };
};

} // namespace linernotes::library

Q_DECLARE_METATYPE(linernotes::library::ScanStats)
Q_DECLARE_METATYPE(linernotes::library::ScanProgress)
