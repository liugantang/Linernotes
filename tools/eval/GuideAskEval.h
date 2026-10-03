// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include "EvalHarness.h"

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>

#include <guide/DeepDiveSession.h>
#include <guide/DeepDiveTools.h>
#include <guide/KnowledgeService.h>
#include <guide/LyricsService.h>

#include <memory>

namespace linernotes::eval {

class GuideAskEval : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(GuideAskEval)

public:
    explicit GuideAskEval(EvalConfig config, QObject *parent = nullptr);
    ~GuideAskEval() override;

    bool init();
    void start();

private slots:
    void onToolUsed(const QString &name, const QString &summary);
    void onDelta(const QString &text);
    void onTurnFinished(const linernotes::core::Result<QString> &answer,
        const linernotes::guide::DeepDiveSourceList &sources);

private:
    void askNextQuestion();

    EvalHarness m_harness;
    std::unique_ptr<guide::KnowledgeService> m_knowledgeService;
    std::unique_ptr<guide::LyricsService> m_lyricsService;
    std::unique_ptr<guide::DeepDiveTools> m_tools;
    std::unique_ptr<guide::DeepDiveSession> m_session;

    QStringList m_questions;
    int m_currentQuestionIndex = 0;
    QElapsedTimer m_turnTimer;
};

} // namespace linernotes::eval
