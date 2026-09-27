// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "QmlTypes.h"

linernotes::ui::AppContext *AppContextForeign::s_instance = nullptr;

void AppContextForeign::setInstance(linernotes::ui::AppContext *instance)
{
    s_instance = instance;
}

linernotes::ui::AppContext *AppContextForeign::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)
    if (s_instance != nullptr) {
        QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    }
    return s_instance;
}
