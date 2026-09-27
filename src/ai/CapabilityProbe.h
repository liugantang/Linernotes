// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>

#include <ai/ChatTypes.h>
#include <ai/JsonSchema.h>
#include <ai/LlmClient.h>
#include <ai/LlmReply.h>
#include <ai/StructuredOutput.h>
#include <core/Result.h>

#include <memory>

namespace linernotes::ai {

/// 对一个服务依次做三项探测（串行，避免本地服务并发吃不消）：
/// 1. json_schema：要求输出 {"ok": true}（schema: object, 必填 boolean ok, additionalProperties
/// false）
/// 2. tools：强制调用工具 report(ok: boolean)
/// 3. json_object：要求输出 {"ok": true}
/// 某项判定为“支持”的条件：2xx，且按对应 StructuredMode 用 parseStructuredResponse 能解析出符合
/// schema 的值。 HTTP 400/404/422/500 等非 2xx（服务不认识该参数时常见）或解析失败 →
/// 该项不支持，继续下一项。 但 errc::kNetwork / kTimeout / kAuth / kRateLimited
/// 说明服务本身不可用：立即结束并报告该错误。 三项都结束后以 Capabilities 结束。 探测请求
/// temperature=0、maxTokens=50。
class CapabilityProbe : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(CapabilityProbe)
public:
    explicit CapabilityProbe(LlmClient &client, ServiceConfig service, QObject *parent = nullptr);
    ~CapabilityProbe() override = default;

    void start(); // 只能调用一次
    const core::Result<Capabilities> &result() const; // finished 之后可调用

signals:
    void finished();

private:
    enum class Step : std::uint8_t { JsonSchema, Tools, JsonObject, Done };

    void runCurrentStep();
    void onReplyFinished();
    void finishWithError(core::Error error);
    void finishWithSuccess(Capabilities caps);
    StructuredSpec currentSpec() const;
    StructuredMode currentMode() const;
    void advanceStep();
    void setCapability(bool supported);

    LlmClient &m_client;
    ServiceConfig m_service;
    std::unique_ptr<LlmReply> m_currentReply;
    JsonSchema m_schema;
    Capabilities m_caps;
    core::Result<Capabilities> m_result { core::Error { } };
    Step m_currentStep = Step::JsonSchema;
    bool m_started = false;
    bool m_finished = false;
};

} // namespace linernotes::ai
