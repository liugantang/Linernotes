// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <ai/LlmService.h>
#include <ai/PromptLibrary.h>
#include <core/Result.h>
#include <guide/Knowledge.h>

#include <map>
#include <memory>

namespace linernotes::guide {

QHash<QString, QString> knowledgeCardPromptVars(const KnowledgeEntry &entry);

class KnowledgeCardWriter : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(KnowledgeCardWriter)

public:
    explicit KnowledgeCardWriter(
        ai::LlmService &llm, ai::PromptLibrary &prompts, QObject *parent = nullptr);
    ~KnowledgeCardWriter() override;

    void write(const QString &key, const KnowledgeEntry &entry);
    void cancelAll();

signals:
    void delta(const QString &key, const QString &text);
    void finished(const QString &key, const linernotes::core::Result<QString> &result);

private:
    ai::LlmService &m_llm;
    ai::PromptLibrary &m_prompts;
    std::map<QString, std::unique_ptr<ai::LlmTask>> m_tasks; // QHash 要求值可复制
};

} // namespace linernotes::guide
