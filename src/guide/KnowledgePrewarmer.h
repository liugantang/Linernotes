// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <core/Clock.h>
#include <core/Settings.h>
#include <guide/GuideSettings.h>
#include <guide/Knowledge.h>
#include <guide/KnowledgeService.h>
#include <library/Database.h>

#include <cstdint>

namespace linernotes::guide {

class KnowledgePrewarmer : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(KnowledgePrewarmer)

public:
    explicit KnowledgePrewarmer(library::Database &db, KnowledgeService &knowledgeService,
        const core::Clock &clock, core::Settings &settings, QObject *parent = nullptr);
    ~KnowledgePrewarmer() override;

    void start();
    void stop();
    [[nodiscard]] bool isRunning() const;
    void maybeStart();

signals:
    void runningChanged();

private slots:
    void onKnowledgeFinished(const linernotes::guide::KnowledgeResult &result);

private:
    enum class Step : std::uint8_t {
        Artist,
        Song,
        Album,
    };

    struct PrewarmTrack {
        qint64 trackId = 0;
        QString artist { };
        QString title { };
        QString album { };
    };

    void processNextTrack();
    void processStep();
    void finishRound();

    library::Database &m_db;
    KnowledgeService &m_knowledgeService;
    const core::Clock &m_clock;
    core::Settings &m_settings;

    bool m_running = false;
    QList<PrewarmTrack> m_pendingTracks;
    int m_currentTrackIndex = 0;
    Step m_currentStep = Step::Artist;
    QString m_pendingKey;
    int m_consecutiveFailures = 0;
};

} // namespace linernotes::guide
