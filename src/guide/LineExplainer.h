// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

#include <ai/LlmService.h>
#include <ai/PromptLibrary.h>
#include <core/Result.h>
#include <guide/LrcParser.h>

#include <memory>

namespace linernotes::guide {

struct LineExplainRequest {
    qint64 trackId = 0;
    int lineIndex = -1;
    QString title { };
    QString artist { };
    QString album { };
    ParsedLyrics lyrics { };
    QStringList translations; // 与 lyrics.lines 等长或为空
};

// 纯函数（便于测试）：组提示词变量；lyrics 中跳过空行；context 取 lineIndex 前后各 2 个非空行
QHash<QString, QString> explainLinePromptVars(const LineExplainRequest &req);

class LineExplainer : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(LineExplainer)

public:
    explicit LineExplainer(
        ai::LlmService &llm, ai::PromptLibrary &prompts, QObject *parent = nullptr);
    ~LineExplainer() override;

    // 先 cancel 旧任务；Purpose::Guide、stream = true、非结构化、CachePolicy::Use
    void explain(const LineExplainRequest &req);
    void cancel();

signals:
    void delta(
        qint64 trackId, int lineIndex, const QString &text); // 转发 LlmTask::delta（增量文本）
    void finished(qint64 trackId, int lineIndex,
        const linernotes::core::Result<QString> &result); // 成功时为完整文本（trim）

private:
    ai::LlmService &m_llm;
    ai::PromptLibrary &m_prompts;

    std::unique_ptr<ai::LlmTask> m_currentTask;
    qint64 m_activeTrackId = -1;
    int m_activeLineIndex = -1;
};

} // namespace linernotes::guide
