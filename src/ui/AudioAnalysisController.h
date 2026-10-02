// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>
#include <QString>

namespace linernotes::core {
class Clock;
}

namespace linernotes::library {
class Database;
}

namespace linernotes::ai {
class JobQueue;
}

namespace linernotes::ui {

class AudioAnalysisController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(AudioAnalysisController)

    Q_PROPERTY(bool modelAvailable READ isModelAvailable CONSTANT)
    Q_PROPERTY(QString modelPath READ modelPath CONSTANT)
    Q_PROPERTY(int analyzedCount READ analyzedCount NOTIFY countsChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY countsChanged)
    Q_PROPERTY(bool running READ isRunning NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ isPaused NOTIFY stateChanged)
    Q_PROPERTY(int done READ done NOTIFY progressChanged)
    Q_PROPERTY(int total READ total NOTIFY progressChanged)

public:
    AudioAnalysisController(library::Database &db, const core::Clock &clock, ai::JobQueue &jobs,
        QString modelPath, QObject *parent = nullptr);
    ~AudioAnalysisController() override = default;

    [[nodiscard]] bool isModelAvailable() const;
    [[nodiscard]] QString modelPath() const;
    [[nodiscard]] int analyzedCount() const;
    [[nodiscard]] int pendingCount() const;
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] bool isPaused() const;
    [[nodiscard]] int done() const;
    [[nodiscard]] int total() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void start();
    Q_INVOKABLE void pause();

signals:
    void countsChanged();
    void stateChanged();
    void progressChanged();

private slots:
    void onJobChanged(qint64 jobId);

private:
    void findExistingJob();

    library::Database &m_db;
    const core::Clock &m_clock;
    ai::JobQueue &m_jobs;
    QString m_modelPath;

    int m_analyzedCount = 0;
    int m_pendingCount = 0;
    qint64 m_currentJobId = 0;
    bool m_running = false;
    bool m_paused = false;
    int m_done = 0;
    int m_total = 0;
};

} // namespace linernotes::ui
