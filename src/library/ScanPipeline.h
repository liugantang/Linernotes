// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

/**
 * @file ScanPipeline.h
 * @brief Tag reading and writing pipeline with concurrent workers.
 *
 * 仅供 library 内部使用 (Internal to the library module only).
 */

#include <QHash>
#include <QList>
#include <QString>

#include <core/Result.h>
#include <library/CoverStore.h>
#include <library/ScanDiff.h>
#include <library/ScanWriter.h>
#include <library/Scanner.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

class QSqlDatabase;

namespace linernotes::library::detail {

template <typename T> class BoundedQueue {
public:
    explicit BoundedQueue(size_t maxSize)
        : m_maxSize(std::max<size_t>(1, maxSize))
    {
    }

    void push(T item)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cvNotFull.wait(lock, [this]() { return m_queue.size() < m_maxSize || m_stopped; });
        if (m_stopped) {
            return;
        }
        m_queue.push_back(std::move(item));
        m_cvNotEmpty.notify_one();
    }

    bool pop(T &item)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cvNotEmpty.wait(lock, [this]() { return !m_queue.empty() || m_stopped; });
        if (m_queue.empty()) {
            return false;
        }
        item = std::move(m_queue.front());
        m_queue.pop_front();
        m_cvNotFull.notify_one();
        return true;
    }

    void stop()
    {
        {
            const std::scoped_lock lock(m_mutex);
            m_stopped = true;
        }
        m_cvNotFull.notify_all();
        m_cvNotEmpty.notify_all();
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cvNotFull;
    std::condition_variable m_cvNotEmpty;
    std::deque<T> m_queue;
    size_t m_maxSize;
    bool m_stopped = false;
};

struct FolderCoverEntry {
    bool found = false;
    QString filePath;
    std::optional<CoverStore::Info> info;
};

class FolderCoverCache {
public:
    explicit FolderCoverCache(CoverStore *store); // store 可为空

    FolderCoverEntry findAndIngest(const QString &dirPath);

private:
    FolderCoverEntry doFindAndIngest(const QString &dirPath);

    CoverStore *m_store = nullptr; // 可为空
    std::mutex m_mutex;
    QHash<QString, FolderCoverEntry> m_cache;
};

CoverInfo extractCover(const QString &filePath, const TagReadResult &tagRes,
    CoverStore *coverStore /* 可为空 */, const std::shared_ptr<FolderCoverCache> &folderCoverCache);

core::Result<void> processWriterQueue(const QSqlDatabase &db,
    BoundedQueue<FileReadResult> &resultQueue, int batchSize, int totalToRead,
    const std::function<qint64()> &getNow,
    const std::function<void(const ScanProgress &)> &onProgress, ScanStats &stats);

core::Result<void> executeReadAndWritePipeline(const QSqlDatabase &db,
    const QList<ScannedFile> &filesToRead, const QHash<QString, DbFile> &dbFilesByPath,
    int batchSize, int maxThreads, CoverStore *coverStore /* 可为空 */,
    std::atomic<bool> &cancelled, const std::function<qint64()> &getNow,
    const std::function<void(const ScanProgress &)> &onProgress, ScanStats &stats);

} // namespace linernotes::library::detail
