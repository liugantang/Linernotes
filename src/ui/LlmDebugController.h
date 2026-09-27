// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>
#include <QString>

#include <ai/LlmDebugLog.h>
#include <ai/LlmService.h>
#include <ui/LlmDebugModel.h>

namespace linernotes::ui {

class LlmDebugController : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(LlmDebugController)

    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(linernotes::ui::LlmDebugModel *entries READ entries CONSTANT)

public:
    explicit LlmDebugController(
        ai::LlmDebugLog &log, ai::LlmService &service, QObject *parent = nullptr);
    ~LlmDebugController() override = default;

    [[nodiscard]] bool isEnabled() const;
    void setEnabled(bool enabled);
    [[nodiscard]] LlmDebugModel *entries();

    Q_INVOKABLE [[nodiscard]] QString requestText(quint64 id) const;
    Q_INVOKABLE [[nodiscard]] QString responseText(quint64 id) const;
    Q_INVOKABLE [[nodiscard]] QString errorText(quint64 id) const;
    Q_INVOKABLE void replay(quint64 id);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void copyToClipboard(const QString &text);

signals:
    void enabledChanged();

private:
    ai::LlmDebugLog &m_log;
    ai::LlmService &m_service;
    LlmDebugModel m_model;
};

} // namespace linernotes::ui
