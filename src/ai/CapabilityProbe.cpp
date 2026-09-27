// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "CapabilityProbe.h"

#include "Errors.h"

#include <QJsonArray>
#include <QJsonObject>

#include <utility>

namespace linernotes::ai {

namespace {

QJsonObject makeProbeSchema()
{
    QJsonObject okProp;
    okProp.insert(QStringLiteral("type"), QStringLiteral("boolean"));

    QJsonObject props;
    props.insert(QStringLiteral("ok"), okProp);

    QJsonObject schema;
    schema.insert(QStringLiteral("type"), QStringLiteral("object"));
    schema.insert(QStringLiteral("properties"), props);
    schema.insert(QStringLiteral("required"), QJsonArray { QStringLiteral("ok") });
    schema.insert(QStringLiteral("additionalProperties"), false);
    return schema;
}

} // namespace

CapabilityProbe::CapabilityProbe(LlmClient &client, ServiceConfig service, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_service(std::move(service))
{
    const auto schemaRes = JsonSchema::compile(makeProbeSchema());
    if (schemaRes.ok()) {
        m_schema = schemaRes.value();
    }
}

void CapabilityProbe::start()
{
    if (m_started) {
        return;
    }
    m_started = true;
    m_currentStep = Step::JsonSchema;
    runCurrentStep();
}

const core::Result<Capabilities> &CapabilityProbe::result() const
{
    Q_ASSERT_X(m_finished, "CapabilityProbe::result", "Called result() before probe finished");
    return m_result;
}

StructuredSpec CapabilityProbe::currentSpec() const
{
    const QJsonObject schema = makeProbeSchema();
    switch (m_currentStep) {
    case Step::JsonSchema:
        return {
            .name = QStringLiteral("probe"),
            .description = QStringLiteral("Probe schema"),
            .schema = schema,
        };
    case Step::Tools:
        return {
            .name = QStringLiteral("report"),
            .description = QStringLiteral("Report probe result"),
            .schema = schema,
        };
    case Step::JsonObject:
        return {
            .name = QStringLiteral("probe"),
            .description = QStringLiteral("Probe schema"),
            .schema = schema,
        };
    case Step::Done:
        return { };
    }
    return { };
}

StructuredMode CapabilityProbe::currentMode() const
{
    switch (m_currentStep) {
    case Step::JsonSchema:
        return StructuredMode::JsonSchema;
    case Step::Tools:
        return StructuredMode::Tool;
    case Step::JsonObject:
        return StructuredMode::JsonObject;
    case Step::Done:
        return StructuredMode::Prompt;
    }
    return StructuredMode::Prompt;
}

void CapabilityProbe::setCapability(bool supported)
{
    switch (m_currentStep) {
    case Step::JsonSchema:
        m_caps.jsonSchema = supported;
        break;
    case Step::Tools:
        m_caps.tools = supported;
        break;
    case Step::JsonObject:
        m_caps.jsonObject = supported;
        break;
    case Step::Done:
        break;
    }
}

void CapabilityProbe::advanceStep()
{
    switch (m_currentStep) {
    case Step::JsonSchema:
        m_currentStep = Step::Tools;
        break;
    case Step::Tools:
        m_currentStep = Step::JsonObject;
        break;
    case Step::JsonObject:
        m_currentStep = Step::Done;
        break;
    case Step::Done:
        break;
    }
}

void CapabilityProbe::runCurrentStep()
{
    if (m_currentStep == Step::Done) {
        finishWithSuccess(m_caps);
        return;
    }

    const StructuredSpec spec = currentSpec();
    const StructuredMode mode = currentMode();

    ChatRequest base;
    base.messages.append(makeMessage(Role::User, QStringLiteral("Respond with ok: true")));
    base.temperature = 0.0;
    base.maxTokens = 50;

    const ChatRequest req = buildStructuredRequest(std::move(base), spec, mode);
    m_currentReply = m_client.complete(m_service, req);
    if (m_currentReply != nullptr) {
        connect(m_currentReply.get(), &LlmReply::finished, this, &CapabilityProbe::onReplyFinished);
    }
}

void CapabilityProbe::onReplyFinished()
{
    if (m_currentReply == nullptr) {
        return;
    }

    const auto replyRes = m_currentReply->result();
    if (!replyRes.ok()) {
        const QString &errCode = replyRes.error().code;
        if (errCode == errc::kNetwork || errCode == errc::kTimeout || errCode == errc::kAuth
            || errCode == errc::kRateLimited) {
            finishWithError(replyRes.error());
            return;
        }
        // Non-fatal error (e.g. HTTP 400/404/500, BadResponse): unsupported, continue to next step
        advanceStep();
        runCurrentStep();
        return;
    }

    const StructuredSpec spec = currentSpec();
    const StructuredMode mode = currentMode();
    const auto parseRes = parseStructuredResponse(replyRes.value(), spec, m_schema, mode);
    if (parseRes.ok()) {
        setCapability(true);
    }

    advanceStep();
    runCurrentStep();
}

void CapabilityProbe::finishWithError(core::Error error)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    m_result = std::move(error);
    m_currentReply.reset();
    emit finished();
}

void CapabilityProbe::finishWithSuccess(Capabilities caps)
{
    if (m_finished) {
        return;
    }
    m_finished = true;
    m_result = caps;
    m_currentReply.reset();
    emit finished();
}

} // namespace linernotes::ai
