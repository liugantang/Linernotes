// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "Translations.h"

#include "UiLogging.h"

#include <QLibraryInfo>
#include <QLocale>

namespace linernotes::ui {

Translations::Translations(QCoreApplication &app, SettingsController &settings, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_settings(settings)
{
    connect(&m_settings, &SettingsController::languageChanged, this, [this]() {
        apply(m_settings.language());
        emit retranslateRequested();
    });
}

void Translations::apply(SettingsController::Language language)
{
    QCoreApplication::removeTranslator(&m_appTranslator);
    QCoreApplication::removeTranslator(&m_qtTranslator);

    bool isChinese = false;
    switch (language) {
    case SettingsController::Language::System: {
        const QString sysName = QLocale::system().name();
        isChinese = sysName.startsWith(u"zh", Qt::CaseInsensitive);
        break;
    }
    case SettingsController::Language::Chinese:
        isChinese = true;
        break;
    case SettingsController::Language::English:
        isChinese = false;
        break;
    }

    // 英文是源语言，但 “%n track(s)” 的单复数形式来自 linernotes_en.qm
    const QString appQm = isChinese ? QStringLiteral(":/i18n/linernotes_zh_CN.qm")
                                    : QStringLiteral(":/i18n/linernotes_en.qm");
    if (m_appTranslator.load(appQm)) {
        QCoreApplication::installTranslator(&m_appTranslator);
    } else {
        qCInfo(lcUi) << "Could not load application translation" << appQm;
    }

    if (!isChinese) {
        return;
    }

    const QString qtTranslationsPath = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    if (m_qtTranslator.load(QStringLiteral("qtbase_zh_CN"), qtTranslationsPath)) {
        QCoreApplication::installTranslator(&m_qtTranslator);
    } else {
        qCInfo(lcUi) << "Could not load qtbase_zh_CN translation from" << qtTranslationsPath;
    }
}

} // namespace linernotes::ui
