// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QDBusAbstractAdaptor>
#include <QString>
#include <QStringList>

namespace linernotes::ui {

class Mpris;

class MprisRootAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MprisRootAdaptor)
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool CanSetFullscreen READ canSetFullscreen)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)

public:
    explicit MprisRootAdaptor(Mpris *parent);
    ~MprisRootAdaptor() override = default;

    [[nodiscard]] bool canQuit() const;
    [[nodiscard]] bool canRaise() const;
    [[nodiscard]] bool canSetFullscreen() const;
    [[nodiscard]] bool hasTrackList() const;
    [[nodiscard]] QString identity() const;
    [[nodiscard]] QString desktopEntry() const;
    [[nodiscard]] QStringList supportedUriSchemes() const;
    [[nodiscard]] QStringList supportedMimeTypes() const;

public slots:
    // NOLINTBEGIN(readability-identifier-naming) - D-Bus method names are fixed by the MPRIS spec
    void Raise();
    void Quit();
    // NOLINTEND(readability-identifier-naming)

private:
    Mpris *m_mpris = nullptr;
};

} // namespace linernotes::ui
