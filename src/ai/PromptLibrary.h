// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <ai/ChatTypes.h>
#include <core/Result.h>

namespace linernotes::ai {

struct PromptTemplate {
    QString id;
    int version = 0;
    QString description;
    QString system; // 可为空
    QString user;
    QString path; // 实际读取的文件，便于调试面板/日志显示
    bool operator==(const PromptTemplate &) const = default;
};

struct RenderedPrompt {
    QString id;
    int version = 0;
    QString system;
    QString user;
    bool operator==(const RenderedPrompt &) const = default;
};

class PromptLibrary {
public:
    /// dirs 按优先级从高到低，例如 { <configDir>/prompts, ":/prompts" }。
    explicit PromptLibrary(QStringList dirs);

    /// 每次调用都重新读文件（文件很小；用户改完即生效）。
    [[nodiscard]] core::Result<PromptTemplate> load(const QString &id) const;
    [[nodiscard]] core::Result<RenderedPrompt> render(
        const QString &id, const QHash<QString, QString> &vars) const; // load + renderTemplate

    [[nodiscard]] static core::Result<PromptTemplate> parse(const QString &id, const QString &text);
    [[nodiscard]] static core::Result<RenderedPrompt> renderTemplate(
        const PromptTemplate &tmpl, const QHash<QString, QString> &vars);

private:
    QStringList m_dirs;
};

/// system 为空时只返回一条 user 消息。
QList<ChatMessage> toMessages(const RenderedPrompt &prompt);

} // namespace linernotes::ai
