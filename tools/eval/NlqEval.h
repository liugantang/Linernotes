// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include "EvalHarness.h"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>

#include <nlq/Interpreter.h>
#include <nlq/LibrarySummary.h>
#include <nlq/NlqQuery.h>

#include <memory>
#include <optional>

namespace linernotes::eval {

[[nodiscard]] bool nlqQueryEquivalent(const nlq::Query &actual, const nlq::Query &expected);

struct NlqCorpusItem {
    QString question;
    std::optional<nlq::Query> previous;
    QList<nlq::Query> expected;
};

struct NlqCorpus {
    QString today;
    QString timeZone;
    QList<NlqCorpusItem> items;
};

struct NlqErrorItem {
    QString question;
    QList<nlq::Query> expected;
    std::optional<nlq::Query> actual;
    QString explanation;
    QString errorReason;
    bool isError = false;
};

class NlqEval : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(NlqEval)

public:
    explicit NlqEval(EvalConfig config, QObject *parent = nullptr);
    ~NlqEval() override;

    bool init();
    void start();

private:
    bool loadCorpus();
    void runNextItem();
    void onInterpreterFinished();
    void finishEvaluation();

    EvalHarness m_harness;
    NlqCorpus m_corpus;
    QString m_librarySummaryText;
    std::unique_ptr<nlq::Interpreter> m_interpreter;
    QElapsedTimer m_totalTimer;
    QElapsedTimer m_itemTimer;

    int m_currentIndex = 0;
    int m_correctCount = 0;
    int m_failedCount = 0;
    qint64 m_totalPromptTokens = 0;
    qint64 m_totalCompletionTokens = 0;
    QList<NlqErrorItem> m_errors;
};

} // namespace linernotes::eval
