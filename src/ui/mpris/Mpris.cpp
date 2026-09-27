// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "Mpris.h"

#include "MprisPlayerAdaptor.h"
#include "MprisRootAdaptor.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QLatin1StringView>

namespace linernotes::ui {

namespace {
constexpr QLatin1StringView kErrBusNotConnected { "mpris.bus_not_connected" };
constexpr QLatin1StringView kErrRegisterObjectFailed { "mpris.register_object_failed" };
constexpr QLatin1StringView kErrRegisterServiceFailed { "mpris.register_service_failed" };
} // namespace

Mpris::Mpris(player::Player &player, NowPlaying &nowPlaying, const library::CoverStore &covers,
    QObject *parent)
    : QObject(parent)
    , m_player(player)
    , m_nowPlaying(nowPlaying)
    , m_covers(covers)
{
    new MprisRootAdaptor(this);
    new MprisPlayerAdaptor(this, m_player, m_nowPlaying, m_covers);
}

Mpris::~Mpris()
{
    if (m_registered) {
        auto bus = QDBusConnection::sessionBus();
        if (bus.isConnected()) {
            bus.unregisterObject(QStringLiteral("/org/mpris/MediaPlayer2"));
            if (!m_serviceName.isEmpty()) {
                bus.unregisterService(m_serviceName);
            }
        }
    }
}

core::Result<void> Mpris::registerOnBus()
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        return core::Error {
            .code = QString(kErrBusNotConnected),
            .message = QStringLiteral("D-Bus session bus is not connected"),
            .detail = { },
        };
    }

    const QString objectPath = QStringLiteral("/org/mpris/MediaPlayer2");
    if (!bus.registerObject(objectPath, this, QDBusConnection::ExportAdaptors)) {
        return core::Error {
            .code = QString(kErrRegisterObjectFailed),
            .message = QStringLiteral("Failed to register D-Bus object at %1: %2")
                .arg(objectPath, bus.lastError().message()),
            .detail = { },
        };
    }

    const QString baseServiceName = QStringLiteral("org.mpris.MediaPlayer2.linernotes");
    if (bus.registerService(baseServiceName)) {
        m_serviceName = baseServiceName;
        m_registered = true;
        return { };
    }

    const QString instanceServiceName
        = QStringLiteral("org.mpris.MediaPlayer2.linernotes.instance%1")
              .arg(QCoreApplication::applicationPid());
    if (bus.registerService(instanceServiceName)) {
        m_serviceName = instanceServiceName;
        m_registered = true;
        return { };
    }

    bus.unregisterObject(objectPath);
    return core::Error {
        .code = QString(kErrRegisterServiceFailed),
        .message = QStringLiteral("Failed to register D-Bus service (%1 / %2): %3")
            .arg(baseServiceName, instanceServiceName, bus.lastError().message()),
        .detail = { },
    };
}

} // namespace linernotes::ui
