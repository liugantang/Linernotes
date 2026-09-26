// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QJSEngine>
#include <QObject>
#include <QQmlEngine>

#include <player/PlayMode.h>
#include <player/PlayQueue.h>
#include <player/Player.h>
#include <ui/AlbumGridModel.h>
#include <ui/AppContext.h>
#include <ui/ArtistListModel.h>
#include <ui/LibraryActions.h>
#include <ui/RowSelection.h>
#include <ui/TrackListModel.h>

struct AppContextForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::AppContext)
    QML_NAMED_ELEMENT(AppContext)
    QML_SINGLETON

public:
    static void setInstance(linernotes::ui::AppContext *instance);
    static linernotes::ui::AppContext *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

private:
    static linernotes::ui::AppContext *s_instance;
};

struct PlayerForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::player::Player)
    QML_NAMED_ELEMENT(Player)
    QML_UNCREATABLE("Player is managed by AppContext")
};

struct PlayQueueForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::player::PlayQueue)
    QML_NAMED_ELEMENT(PlayQueue)
    QML_UNCREATABLE("PlayQueue is managed by Player")
};

struct PlayModeForeign {
    Q_GADGET
    QML_FOREIGN_NAMESPACE(linernotes::player)
    QML_NAMED_ELEMENT(PlayMode)
};

struct LibraryActionsForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::LibraryActions)
    QML_NAMED_ELEMENT(LibraryActions)
    QML_UNCREATABLE("LibraryActions is managed by AppContext")
};

struct RowSelectionForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::RowSelection)
    QML_NAMED_ELEMENT(RowSelection)
};

struct TrackListModelForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::TrackListModel)
    QML_NAMED_ELEMENT(TrackListModel)
};

struct AlbumGridModelForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::AlbumGridModel)
    QML_NAMED_ELEMENT(AlbumGridModel)
};

struct ArtistListModelForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::ArtistListModel)
    QML_NAMED_ELEMENT(ArtistListModel)
};
