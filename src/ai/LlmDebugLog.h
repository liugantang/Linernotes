// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

#include <ai/AiEnums.h>
#include <ai/ChatTypes.h>
#include <ai/LlmService.h>
#include <core/Settings.h>

#include <cstdint>
#include <optional>

namespace linernotes::ai {

struct LlmDebugEntry {
    quint64 id = 0; // 自增
    qint64 startedAtMs = 0;
    Purpose purpose = Purpose::Query;
    QString serviceId { };
    QString model { };
    int attempt = 0; // 第几次尝试（缓存命中为 0）
    bool fromCache = false;
    QJsonObject requestJson { }; // toRequestJson 的结果（不含任何请求头，天然不含 Key）
    QString responseText { }; // 回复内容；有 tool_calls 时附上其 JSON；失败时为空
    int httpStatus = 0;
    QString errorCode { };
    QString errorMessage { };
    qint64 elapsedMs = 0;
    TokenUsage usage { };
    LlmCall call { }; // 原始调用，供重放
};

class LlmDebugLog : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(LlmDebugLog)

public:
    explicit LlmDebugLog(core::Settings &settings, QObject *parent = nullptr);
    ~LlmDebugLog() override = default;

    [[nodiscard]] bool isEnabled() const; // Settings key developer/llmDebug，默认 false
    void setEnabled(bool enabled); // 关闭时清空已记录条目
    void add(LlmDebugEntry entry); // 未启用时直接丢弃；超过 200 条丢最旧的；分配 id
    [[nodiscard]] const QList<LlmDebugEntry> &entries() const; // 旧 → 新
    [[nodiscard]] std::optional<LlmDebugEntry> entry(quint64 id) const;
    void clear();

signals:
    void enabledChanged();
    void entryAdded(quint64 id);
    void cleared();

private:
    core::Settings &m_settings;
    QList<LlmDebugEntry> m_entries;
    quint64 m_nextId = 0;
};

} // namespace linernotes::ai
