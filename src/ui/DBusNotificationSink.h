// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>
#include <QString>

#include <ui/NotificationSink.h>

#include <cstdint>

namespace linernotes::ui {

class DBusNotificationSink : public QObject, public NotificationSink {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(DBusNotificationSink)

public:
    explicit DBusNotificationSink(QObject *parent = nullptr);
    ~DBusNotificationSink() override = default;

    void show(const QString &summary, const QString &body, const QString &iconPath) override;

private:
    std::uint32_t m_replacesId { 0 };
};

} // namespace linernotes::ui
