// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#include "MprisRootAdaptor.h"

#include "Mpris.h"

#include <QCoreApplication>

namespace linernotes::ui {

MprisRootAdaptor::MprisRootAdaptor(Mpris *parent)
    : QDBusAbstractAdaptor(parent)
    , m_mpris(parent)
{
}

bool MprisRootAdaptor::canQuit() const
{
    return true;
}

bool MprisRootAdaptor::canRaise() const
{
    return true;
}

bool MprisRootAdaptor::canSetFullscreen() const
{
    return false;
}

bool MprisRootAdaptor::hasTrackList() const
{
    return false;
}

QString MprisRootAdaptor::identity() const
{
    return QStringLiteral("Linernotes");
}

QString MprisRootAdaptor::desktopEntry() const
{
    return QStringLiteral("linernotes");
}

QStringList MprisRootAdaptor::supportedUriSchemes() const
{
    return { QStringLiteral("file") };
}

QStringList MprisRootAdaptor::supportedMimeTypes() const
{
    return {
        QStringLiteral("audio/flac"),
        QStringLiteral("audio/mpeg"),
        QStringLiteral("audio/ogg"),
        QStringLiteral("audio/opus"),
        QStringLiteral("audio/wav"),
        QStringLiteral("audio/x-wav"),
        QStringLiteral("audio/mp4"),
        QStringLiteral("audio/aac"),
        QStringLiteral("audio/x-wavpack"),
    };
}

void MprisRootAdaptor::Raise()
{
    if (m_mpris != nullptr) {
        emit m_mpris->raiseRequested();
    }
}

void MprisRootAdaptor::Quit()
{
    QCoreApplication::quit();
}

} // namespace linernotes::ui
