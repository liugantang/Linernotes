// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>

class AppInfo : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    explicit AppInfo(QObject *parent = nullptr);
    ~AppInfo() override = default;

    Q_DISABLE_COPY_MOVE(AppInfo)

    [[nodiscard]] QString name() const;
    [[nodiscard]] QString version() const;
};
