// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "KnowledgeCardWriter.h"

#include <ai/AiEnums.h>
#include <ai/ChatTypes.h>
#include <guide/Wikidata.h>

#include <utility>

namespace linernotes::guide {

QHash<QString, QString> knowledgeCardPromptVars(const KnowledgeEntry &entry)
{
    QHash<QString, QString> vars;
    switch (entry.kind) {
    case KnowledgeKind::Artist:
        vars.insert(QStringLiteral("kind"), QStringLiteral("艺人"));
        break;
    case KnowledgeKind::Song:
        vars.insert(QStringLiteral("kind"), QStringLiteral("歌曲"));
        break;
    case KnowledgeKind::Album:
        vars.insert(QStringLiteral("kind"), QStringLiteral("专辑"));
        break;
    }

    vars.insert(QStringLiteral("name"), entry.label);

    QStringList factLines;
    factLines.reserve(entry.facts.size());
    for (const auto &fact : entry.facts) {
        factLines.append(factPropertyName(fact.property) + QStringLiteral("：") + fact.value);
    }
    vars.insert(QStringLiteral("facts"), factLines.join(QLatin1Char('\n')));

    if (entry.summary.has_value()) {
        vars.insert(QStringLiteral("extract"), entry.summary->extract);
        vars.insert(QStringLiteral("extract_lang"), entry.summary->lang);
    } else {
        vars.insert(QStringLiteral("extract"), QString());
        vars.insert(QStringLiteral("extract_lang"), QString());
    }

    return vars;
}

KnowledgeCardWriter::KnowledgeCardWriter(
    ai::LlmService &llm, ai::PromptLibrary &prompts, QObject *parent)
    : QObject(parent)
    , m_llm(llm)
    , m_prompts(prompts)
{
}

KnowledgeCardWriter::~KnowledgeCardWriter()
{
    cancelAll();
}

void KnowledgeCardWriter::write(const QString &key, const KnowledgeEntry &entry)
{
    if (key.isEmpty()) {
        return;
    }

    if (auto it = m_tasks.find(key); it != m_tasks.end()) {
        auto oldTask = std::move(it->second);
        m_tasks.erase(it);
        if (oldTask != nullptr) {
            oldTask->abort();
        }
    }

    const auto vars = knowledgeCardPromptVars(entry);
    const auto renderedRes = m_prompts.render(QStringLiteral("guide/knowledge_card"), vars);
    if (!renderedRes.ok()) {
        emit finished(key, renderedRes.error());
        return;
    }

    ai::ChatRequest chatReq;
    chatReq.messages = ai::toMessages(renderedRes.value());

    ai::LlmCall call {
        .purpose = ai::Purpose::Guide,
        .request = std::move(chatReq),
        .structured = std::nullopt,
        .stream = true,
        .cachePolicy = ai::CachePolicy::Use,
        .cacheTtlMs = std::nullopt,
        .dataCategories = { },
    };

    auto task = m_llm.start(std::move(call));
    auto *taskPtr = task.get();
    m_tasks.insert_or_assign(key, std::move(task));

    connect(taskPtr, &ai::LlmTask::delta, this, [this, taskPtr, key](const QString &text) {
        if (auto it = m_tasks.find(key); it != m_tasks.end() && it->second.get() == taskPtr) {
            emit delta(key, text);
        }
    });

    connect(taskPtr, &ai::LlmTask::finished, this, [this, taskPtr, key]() {
        auto it = m_tasks.find(key);
        if (it == m_tasks.end() || it->second.get() != taskPtr) {
            return;
        }
        const auto finishedTask = std::move(it->second);
        m_tasks.erase(it);

        const auto &res = finishedTask->result();
        if (!res.ok()) {
            emit finished(key, res.error());
            return;
        }

        const auto &llmResult = res.value();
        const QString fullText = llmResult.response.content.trimmed();
        emit finished(key, fullText);
    });
}

void KnowledgeCardWriter::cancelAll()
{
    auto tasks = std::move(m_tasks);
    m_tasks.clear();
    for (auto &entry : tasks) {
        if (entry.second != nullptr) {
            entry.second->abort();
        }
    }
}

} // namespace linernotes::guide
