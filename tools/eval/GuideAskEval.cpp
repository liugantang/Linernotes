// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "GuideAskEval.h"

#include <QCoreApplication>

#include <library/LibraryQuery.h>

#include <iostream>
#include <utility>

namespace linernotes::eval {

GuideAskEval::GuideAskEval(EvalConfig config, QObject *parent)
    : QObject(parent)
    , m_harness(std::move(config), this)
    , m_questions(m_harness.config().questions)
{
}

GuideAskEval::~GuideAskEval() = default;

bool GuideAskEval::init()
{
    if (!m_harness.init()) {
        return false;
    }

    m_knowledgeService = std::make_unique<guide::KnowledgeService>(
        m_harness.db(), m_harness.network(), m_harness.clock(), this);
    m_lyricsService = std::make_unique<guide::LyricsService>(
        m_harness.db(), m_harness.network(), m_harness.clock(), this);
    m_tools = std::make_unique<guide::DeepDiveTools>(
        m_harness.db(), *m_knowledgeService, *m_lyricsService, m_harness.clock(), this);
    m_session = std::make_unique<guide::DeepDiveSession>(
        m_harness.llm(), m_harness.prompts(), m_harness.aiConfig(), *m_tools, this);

    connect(&*m_session, &guide::DeepDiveSession::toolUsed, this, &GuideAskEval::onToolUsed);
    connect(&*m_session, &guide::DeepDiveSession::delta, this, &GuideAskEval::onDelta);
    connect(
        &*m_session, &guide::DeepDiveSession::turnFinished, this, &GuideAskEval::onTurnFinished);

    return true;
}

void GuideAskEval::start()
{
    if (m_questions.isEmpty()) {
        std::cerr << "Error: No questions provided\n";
        QCoreApplication::exit(2);
        return;
    }

    auto &db = m_harness.db();
    auto connRes = db.connection();
    if (!connRes.ok()) {
        std::cerr << "Failed to get db connection: " << qPrintable(connRes.error().toString())
                  << "\n";
        QCoreApplication::exit(2);
        return;
    }

    const library::LibraryQuery libQuery(connRes.value());
    const auto trackRes = libQuery.tracksByIds({ m_harness.config().trackId });
    if (!trackRes.ok() || trackRes.value().isEmpty()) {
        std::cerr << "Track ID " << m_harness.config().trackId << " not found in database\n";
        QCoreApplication::exit(2);
        return;
    }

    const auto &row = trackRes.value().constFirst();
    guide::DeepDiveContext ctx;
    ctx.trackId = m_harness.config().trackId;
    ctx.title = row.title;
    ctx.artist = row.artist;
    ctx.album = row.album;
    ctx.year = row.year;

    std::cout << "=== Deep Dive Context ===\n";
    std::cout << "  Track ID: " << ctx.trackId << "\n";
    std::cout << "  Title:    " << ctx.title.toStdString() << "\n";
    std::cout << "  Artist:   " << ctx.artist.toStdString() << "\n";
    std::cout << "  Album:    " << ctx.album.toStdString() << "\n";
    if (ctx.year.has_value()) {
        std::cout << "  Year:     " << *ctx.year << "\n";
    }
    std::cout << "\n";

    m_session->reset(ctx);
    m_currentQuestionIndex = 0;
    askNextQuestion();
}

void GuideAskEval::askNextQuestion()
{
    if (m_currentQuestionIndex >= m_questions.size()) {
        m_harness.printLlmUsage();
        QCoreApplication::exit(0);
        return;
    }

    const QString &q = m_questions.at(m_currentQuestionIndex);
    std::cout << ">>> " << q.toStdString() << "\n";
    m_turnTimer.start();
    m_session->ask(q);
}

void GuideAskEval::onToolUsed(const QString &name, const QString &summary)
{
    Q_UNUSED(name);
    std::cout << "\n[tool] " << summary.toStdString() << "\n";
}

void GuideAskEval::onDelta(const QString &text)
{
    std::cout << text.toStdString() << std::flush;
}

void GuideAskEval::onTurnFinished(
    const core::Result<QString> &answer, const guide::DeepDiveSourceList &sources)
{
    if (!answer.ok()) {
        std::cerr << "\nTurn failed: " << qPrintable(answer.error().toString()) << "\n";
        QCoreApplication::exit(1);
        return;
    }

    std::cout << "\n";
    if (!sources.isEmpty()) {
        std::cout << "[sources]";
        for (const auto &src : sources) {
            std::cout << " " << src.label.toStdString() << " (" << src.url.toString().toStdString()
                      << ")";
        }
        std::cout << "\n";
    }
    std::cout << "[elapsed] " << m_turnTimer.elapsed() << " ms\n\n";

    ++m_currentQuestionIndex;
    askNextQuestion();
}

} // namespace linernotes::eval
