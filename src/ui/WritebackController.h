// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>
#include <QString>

#include <library/WritebackStore.h>

namespace linernotes::core {
class Clock;
}

namespace linernotes::library {
class Database;
class Scanner;
} // namespace linernotes::library

namespace linernotes::ai {
class JobQueue;
}

namespace linernotes::ui {

class WritebackController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(WritebackController)

    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(int done READ done NOTIFY progressChanged)
    Q_PROPERTY(int total READ total NOTIFY progressChanged)
    Q_PROPERTY(int failed READ failed NOTIFY progressChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY summaryChanged)

public:
    WritebackController(library::Database &db, const core::Clock &clock, ai::JobQueue &jobs,
        library::Scanner &scanner, QObject *parent = nullptr);
    ~WritebackController() override = default;

    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] int done() const;
    [[nodiscard]] int total() const;
    [[nodiscard]] int failed() const;
    [[nodiscard]] QString summary() const;

    Q_INVOKABLE int writableFileCount(qint64 batchId);
    Q_INVOKABLE bool hasActiveWriteback(qint64 batchId);
    Q_INVOKABLE void startWriteback(qint64 batchId);
    Q_INVOKABLE void startRevert(qint64 batchId);

signals:
    void runningChanged();
    void progressChanged();
    void summaryChanged();
    void libraryModified();

private:
    void onJobChanged(qint64 jobId);
    void finishCurrentJob(bool cancelled);

    library::Database &m_db;
    const core::Clock &m_clock;
    ai::JobQueue &m_jobs;
    library::Scanner &m_scanner;
    library::WritebackStore m_store;

    bool m_running = false;
    qint64 m_currentJobId = 0;
    qint64 m_currentWritebackId = 0;
    bool m_isRevertJob = false;
    int m_done = 0;
    int m_total = 0;
    int m_failed = 0;
    QString m_summary;
};

} // namespace linernotes::ui
