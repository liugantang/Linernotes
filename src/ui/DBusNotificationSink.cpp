// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "DBusNotificationSink.h"

#include "UiLogging.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QStringList>
#include <QVariantMap>

namespace linernotes::ui {

DBusNotificationSink::DBusNotificationSink(QObject *parent)
    : QObject(parent)
{
}

void DBusNotificationSink::show(
    const QString &summary, const QString &body, const QString &iconPath)
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCDebug(lcUi, "D-Bus session bus is not connected");
        return;
    }

    QVariantMap hints;
    hints.insert(QStringLiteral("desktop-entry"), QStringLiteral("linernotes"));
    hints.insert(QStringLiteral("category"), QStringLiteral("x-gnome.music"));

    QDBusMessage msg
        = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.Notifications"),
            QStringLiteral("/org/freedesktop/Notifications"),
            QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));

    msg << QStringLiteral("Linernotes") << static_cast<quint32>(m_replacesId)
        << (iconPath.isEmpty() ? QStringLiteral("audio-x-generic") : iconPath) << summary << body
        << QStringList { } << hints << 5000;

    const QDBusPendingCall pendingCall = bus.asyncCall(msg);
    auto *watcher = new QDBusPendingCallWatcher(pendingCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<quint32> reply = *w;
        if (reply.isError()) {
            qCDebug(lcUi, "D-Bus Notify failed: %s", qPrintable(reply.error().message()));
            return;
        }
        m_replacesId = reply.value();
    });
}

} // namespace linernotes::ui
