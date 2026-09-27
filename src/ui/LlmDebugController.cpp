// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "LlmDebugController.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonDocument>

namespace linernotes::ui {

LlmDebugController::LlmDebugController(
    ai::LlmDebugLog &log, ai::LlmService &service, QObject *parent)
    : QObject(parent)
    , m_log(log)
    , m_service(service)
    , m_model(log, this)
{
    connect(&m_log, &ai::LlmDebugLog::enabledChanged, this, &LlmDebugController::enabledChanged);
}

bool LlmDebugController::isEnabled() const
{
    return m_log.isEnabled();
}

void LlmDebugController::setEnabled(bool enabled)
{
    m_log.setEnabled(enabled);
}

LlmDebugModel *LlmDebugController::entries()
{
    return &m_model;
}

QString LlmDebugController::requestText(quint64 id) const
{
    const auto entryOpt = m_log.entry(id);
    if (!entryOpt.has_value() || entryOpt->requestJson.isEmpty()) {
        return { };
    }
    return QString::fromUtf8(QJsonDocument(entryOpt->requestJson).toJson(QJsonDocument::Indented));
}

QString LlmDebugController::responseText(quint64 id) const
{
    const auto entryOpt = m_log.entry(id);
    if (!entryOpt.has_value()) {
        return { };
    }
    return entryOpt->responseText;
}

QString LlmDebugController::errorText(quint64 id) const
{
    const auto entryOpt = m_log.entry(id);
    if (!entryOpt.has_value()) {
        return { };
    }
    const auto &entry = *entryOpt;
    if (entry.errorCode.isEmpty() && entry.errorMessage.isEmpty()) {
        return { };
    }
    if (entry.errorCode.isEmpty()) {
        return entry.errorMessage;
    }
    if (entry.errorMessage.isEmpty()) {
        return entry.errorCode;
    }
    return QStringLiteral("[%1] %2").arg(entry.errorCode, entry.errorMessage);
}

void LlmDebugController::replay(quint64 id)
{
    const auto entryOpt = m_log.entry(id);
    if (!entryOpt.has_value()) {
        return;
    }

    auto task = m_service.replay(*entryOpt);
    if (task == nullptr) {
        return;
    }

    // 任务归控制器所有；完成后延迟删除（不能在其 finished 信号内同步销毁）
    auto *taskPtr = task.release();
    taskPtr->setParent(this);
    connect(taskPtr, &ai::LlmTask::finished, taskPtr, &QObject::deleteLater);
}

void LlmDebugController::clear()
{
    m_log.clear();
}

void LlmDebugController::copyToClipboard(const QString &text)
{
    auto *clipboard = QGuiApplication::clipboard();
    if (clipboard != nullptr) {
        clipboard->setText(text);
    }
}

} // namespace linernotes::ui
