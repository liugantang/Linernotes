// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "NlqAskEval.h"

#include <QCoreApplication>
#include <QDate>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>

#include <library/LibraryQuery.h>
#include <library/PlayCountRule.h>
#include <nlq/LibrarySummary.h>
#include <nlq/QueryRunner.h>

#include <iostream>

namespace linernotes::eval {

NlqAskEval::NlqAskEval(EvalConfig config, QObject *parent)
    : QObject(parent)
    , m_harness(std::move(config), this)
{
}

NlqAskEval::~NlqAskEval() = default;

bool NlqAskEval::init()
{
    return m_harness.init();
}

void NlqAskEval::start()
{
    auto &db = m_harness.db();
    const QDate today = QDate::currentDate();
    const QString timeZoneId = QString::fromUtf8(QTimeZone::systemTimeZoneId());

    const auto summaryRes = nlq::buildLibrarySummary(db, today, timeZoneId);
    if (!summaryRes.ok()) {
        std::cerr << "Failed to build library summary: "
                  << qPrintable(summaryRes.error().toString()) << "\n";
        QCoreApplication::exit(2);
        return;
    }

    const QString summaryText = nlq::renderLibrarySummary(summaryRes.value());
    std::cout << "=== Library Summary ===\n" << summaryText.toStdString() << "\n\n";

    std::optional<nlq::Query> previousQuery;
    if (!m_harness.config().previous.isEmpty()) {
        QJsonParseError parseErr { };
        const QJsonDocument prevDoc
            = QJsonDocument::fromJson(m_harness.config().previous.toUtf8(), &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !prevDoc.isObject()) {
            std::cerr << "Invalid --previous JSON: " << qPrintable(parseErr.errorString()) << "\n";
            QCoreApplication::exit(2);
            return;
        }
        const auto prevRes = nlq::Query::fromJson(prevDoc.object());
        if (!prevRes.ok()) {
            std::cerr << "Failed to parse previous query: "
                      << qPrintable(prevRes.error().toString()) << "\n";
            QCoreApplication::exit(2);
            return;
        }
        previousQuery = prevRes.value();
    }

    m_interpreter = std::make_unique<nlq::Interpreter>(m_harness.llm(), m_harness.prompts(), this);

    connect(
        m_interpreter.get(), &nlq::Interpreter::finished, this, &NlqAskEval::onInterpreterFinished);

    m_timer.start();
    m_interpreter->interpret(m_harness.config().question, summaryText, previousQuery);
}

void NlqAskEval::onInterpreterFinished()
{
    const auto &res = m_interpreter->result();
    if (!res.ok()) {
        std::cerr << "Interpretation failed: " << qPrintable(res.error().toString()) << "\n";
        QCoreApplication::exit(1);
        return;
    }

    const auto &interp = res.value();
    const QJsonDocument queryDoc(interp.query.toJson());
    std::cout << "=== Query JSON ===\n"
              << queryDoc.toJson(QJsonDocument::Indented).toStdString() << "\n\n";

    std::cout << "=== Explanation ===\n" << interp.explanation.toStdString() << "\n\n";

    auto &db = m_harness.db();
    const nlq::QueryRunner runner(db, library::PlayCountRule { });
    const auto runRes = runner.run(interp.query);
    if (!runRes.ok()) {
        std::cerr << "Query execution failed: " << qPrintable(runRes.error().toString()) << "\n";
        QCoreApplication::exit(1);
        return;
    }

    const auto &ids = runRes.value();
    std::cout << "=== Results (Total: " << ids.size() << ") ===\n";

    auto connRes = db.connection();
    if (!connRes.ok()) {
        std::cerr << "Failed to get db connection for results formatting: "
                  << qPrintable(connRes.error().toString()) << "\n";
        QCoreApplication::exit(1);
        return;
    }

    const library::LibraryQuery libQuery(connRes.value());
    const QList<qint64> topIds = ids.mid(0, 20);

    if (interp.query.entity == nlq::Entity::Track) {
        const auto rowsRes = libQuery.tracksByIds(topIds);
        if (rowsRes.ok()) {
            const auto &rows = rowsRes.value();
            for (qsizetype i = 0; i < rows.size(); ++i) {
                const auto &row = rows.at(i);
                std::cout << "  " << (i + 1) << ". " << row.title.toStdString() << " — "
                          << row.artist.toStdString() << "\n";
            }
        }
    } else if (interp.query.entity == nlq::Entity::Album) {
        const auto rowsRes = libQuery.albumsByIds(topIds);
        if (rowsRes.ok()) {
            const auto &rows = rowsRes.value();
            for (qsizetype i = 0; i < rows.size(); ++i) {
                const auto &row = rows.at(i);
                std::cout << "  " << (i + 1) << ". " << row.title.toStdString() << " — "
                          << row.albumArtist.toStdString() << "\n";
            }
        }
    } else if (interp.query.entity == nlq::Entity::Artist) {
        const auto rowsRes = libQuery.artistsByIds(topIds);
        if (rowsRes.ok()) {
            const auto &rows = rowsRes.value();
            for (qsizetype i = 0; i < rows.size(); ++i) {
                const auto &row = rows.at(i);
                std::cout << "  " << (i + 1) << ". " << row.name.toStdString() << "\n";
            }
        }
    }

    std::cout << "\n=== Stats ===\n";
    std::cout << "  Elapsed Time:      " << m_timer.elapsed() << " ms\n";
    const auto usage = m_interpreter->usage();
    std::cout << "  Prompt Tokens:     " << usage.promptTokens << "\n";
    std::cout << "  Completion Tokens: " << usage.completionTokens << "\n";
    std::cout << "  Total Tokens:      " << (usage.promptTokens + usage.completionTokens) << "\n";

    m_harness.printLlmUsage();

    QCoreApplication::exit(0);
}

} // namespace linernotes::eval
