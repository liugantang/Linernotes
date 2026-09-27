// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QCoreApplication>
#include <QObject>
#include <QTranslator>

#include <ui/SettingsController.h>

namespace linernotes::ui {

class Translations : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Translations)

public:
    explicit Translations(
        QCoreApplication &app, SettingsController &settings, QObject *parent = nullptr);
    ~Translations() override = default;

    void apply(SettingsController::Language language);

signals:
    void retranslateRequested();

private:
    QCoreApplication &m_app;
    SettingsController &m_settings;
    QTranslator m_appTranslator;
    QTranslator m_qtTranslator;
};

} // namespace linernotes::ui
