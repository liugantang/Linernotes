// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QString>

namespace linernotes::ui {

class NotificationSink {
public:
    NotificationSink() = default;
    virtual ~NotificationSink() = default;

    NotificationSink(const NotificationSink &) = default;
    NotificationSink &operator=(const NotificationSink &) = default;
    NotificationSink(NotificationSink &&) = default;
    NotificationSink &operator=(NotificationSink &&) = default;

    /// 显示或替换上一条通知
    virtual void show(const QString &summary, const QString &body, const QString &iconPath) = 0;
};

} // namespace linernotes::ui
