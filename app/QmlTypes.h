// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QJSEngine>
#include <QObject>
#include <QQmlEngine>

#include <library/LibraryEnums.h>
#include <library/SmartRule.h>
#include <player/PlayMode.h>
#include <player/PlayQueue.h>
#include <player/Player.h>
#include <ui/AlbumGridModel.h>
#include <ui/AppContext.h>
#include <ui/ArtistListModel.h>
#include <ui/LibraryActions.h>
#include <ui/LibraryRootsModel.h>
#include <ui/MarksController.h>
#include <ui/NowPlaying.h>
#include <ui/PlaylistController.h>
#include <ui/PlaylistListModel.h>
#include <ui/QueueModel.h>
#include <ui/RowSelection.h>
#include <ui/SearchController.h>
#include <ui/SettingsController.h>
#include <ui/TagEditorModel.h>
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

struct NowPlayingForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::NowPlaying)
    QML_NAMED_ELEMENT(NowPlaying)
    QML_UNCREATABLE("NowPlaying is managed by AppContext")
};

struct QueueModelForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::QueueModel)
    QML_NAMED_ELEMENT(QueueModel)
    QML_UNCREATABLE("QueueModel is managed by AppContext")
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

struct SearchControllerForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::SearchController)
    QML_NAMED_ELEMENT(SearchController)
    QML_UNCREATABLE("SearchController is managed by AppContext")
};

struct PlaylistControllerForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::PlaylistController)
    QML_NAMED_ELEMENT(PlaylistController)
    QML_UNCREATABLE("PlaylistController is managed by AppContext")
};

struct MarksControllerForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::MarksController)
    QML_NAMED_ELEMENT(MarksController)
    QML_UNCREATABLE("MarksController is managed by AppContext")
};

struct SettingsControllerForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::SettingsController)
    QML_NAMED_ELEMENT(SettingsController)
    QML_UNCREATABLE("SettingsController is managed by AppContext")
};

struct TagEditorModelForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::TagEditorModel)
    QML_NAMED_ELEMENT(TagEditorModel)
    QML_UNCREATABLE("TagEditorModel is managed by AppContext")
};

struct LibraryRootsModelForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::LibraryRootsModel)
    QML_NAMED_ELEMENT(LibraryRootsModel)
    QML_UNCREATABLE("LibraryRootsModel is managed by AppContext")
};

struct PlaylistListModelForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::PlaylistListModel)
    QML_NAMED_ELEMENT(PlaylistListModel)
    QML_UNCREATABLE("PlaylistListModel is managed by PlaylistController")
};

struct LibraryForeign {
    Q_GADGET
    QML_FOREIGN_NAMESPACE(linernotes::library)
    QML_NAMED_ELEMENT(Library)
};

struct SmartConditionForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::library::SmartCondition)
    QML_VALUE_TYPE(smartCondition)
    QML_STRUCTURED_VALUE
};

struct SmartRuleForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::library::SmartRule)
    QML_VALUE_TYPE(smartRule)
    QML_STRUCTURED_VALUE
};
