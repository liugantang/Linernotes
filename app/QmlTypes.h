// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

#pragma once

#include <QJSEngine>
#include <QObject>
#include <QQmlEngine>

#include <ai/AiEnums.h>
#include <core/PlaySource.h>
#include <library/ArtistNamePreference.h>
#include <library/LibraryEnums.h>
#include <library/SmartRule.h>
#include <nlq/NlqQuery.h>
#include <player/PlayMode.h>
#include <player/PlayQueue.h>
#include <player/Player.h>
#include <ui/AiSettingsController.h>
#include <ui/AlbumGridModel.h>
#include <ui/AppContext.h>
#include <ui/ArtistListModel.h>
#include <ui/AudioAnalysisController.h>
#include <ui/CleanupController.h>
#include <ui/CorrectionBatchModel.h>
#include <ui/CorrectionListModel.h>
#include <ui/CorrectionReviewController.h>
#include <ui/DuplicateController.h>
#include <ui/LibraryActions.h>
#include <ui/LibraryRootsModel.h>
#include <ui/LlmDebugController.h>
#include <ui/LlmDebugModel.h>
#include <ui/MarksController.h>
#include <ui/NlqController.h>
#include <ui/NowPlaying.h>
#include <ui/PlaylistController.h>
#include <ui/PlaylistListModel.h>
#include <ui/QueueModel.h>
#include <ui/RecommendController.h>
#include <ui/RowSelection.h>
#include <ui/SearchController.h>
#include <ui/ServiceListModel.h>
#include <ui/SettingsController.h>
#include <ui/TagEditorModel.h>
#include <ui/TrackListModel.h>
#include <ui/UsageSummaryModel.h>
#include <ui/WritebackController.h>

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

struct PlaySourceForeign {
    Q_GADGET
    QML_FOREIGN_NAMESPACE(linernotes::core)
    QML_NAMED_ELEMENT(PlaySource)
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

struct NlqControllerForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::NlqController)
    QML_NAMED_ELEMENT(NlqController)
    QML_UNCREATABLE("NlqController is managed by AppContext")
};

struct NlqForeign {
    Q_GADGET
    QML_FOREIGN_NAMESPACE(linernotes::nlq)
    QML_NAMED_ELEMENT(Nlq)
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

struct ArtistNamesForeign {
    Q_GADGET
    QML_FOREIGN_NAMESPACE(linernotes::library::artist_names)
    QML_NAMED_ELEMENT(ArtistNames)
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

struct AiForeign {
    Q_GADGET
    QML_FOREIGN_NAMESPACE(linernotes::ai)
    QML_NAMED_ELEMENT(Ai)
};

struct AiSettingsControllerForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::AiSettingsController)
    QML_NAMED_ELEMENT(AiSettingsController)
    QML_UNCREATABLE("AiSettingsController is managed by AppContext")
};

struct ServiceListModelForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::ServiceListModel)
    QML_NAMED_ELEMENT(ServiceListModel)
    QML_UNCREATABLE("ServiceListModel is managed by AiSettingsController")
};

struct UsageSummaryModelForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::UsageSummaryModel)
    QML_NAMED_ELEMENT(UsageSummaryModel)
    QML_UNCREATABLE("UsageSummaryModel is managed by AiSettingsController")
};

struct LlmDebugControllerForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::LlmDebugController)
    QML_NAMED_ELEMENT(LlmDebugController)
    QML_UNCREATABLE("LlmDebugController is managed by AppContext")
};

struct LlmDebugModelForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::LlmDebugModel)
    QML_NAMED_ELEMENT(LlmDebugModel)
    QML_UNCREATABLE("LlmDebugModel is managed by LlmDebugController")
};

struct CorrectionReviewControllerForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::CorrectionReviewController)
    QML_NAMED_ELEMENT(CorrectionReviewController)
    QML_UNCREATABLE("CorrectionReviewController is managed by AppContext")
};

struct WritebackControllerForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::WritebackController)
    QML_NAMED_ELEMENT(WritebackController)
    QML_UNCREATABLE("WritebackController is managed by AppContext")
};

struct CorrectionBatchModelForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::CorrectionBatchModel)
    QML_NAMED_ELEMENT(CorrectionBatchModel)
    QML_UNCREATABLE("CorrectionBatchModel is managed by CorrectionReviewController")
};

struct CorrectionListModelForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::CorrectionListModel)
    QML_NAMED_ELEMENT(CorrectionListModel)
    QML_UNCREATABLE("CorrectionListModel is managed by CorrectionReviewController")
};

struct CleanupControllerForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::CleanupController)
    QML_NAMED_ELEMENT(CleanupController)
    QML_UNCREATABLE("CleanupController is managed by AppContext")
};

struct DuplicateControllerForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::DuplicateController)
    QML_NAMED_ELEMENT(DuplicateController)
    QML_UNCREATABLE("DuplicateController is managed by AppContext")
};

struct DuplicateSectionModelForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::DuplicateSectionModel)
    QML_NAMED_ELEMENT(DuplicateSectionModel)
    QML_UNCREATABLE("DuplicateSectionModel is managed by DuplicateController")
};

struct AudioAnalysisControllerForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::AudioAnalysisController)
    QML_NAMED_ELEMENT(AudioAnalysisController)
    QML_UNCREATABLE("AudioAnalysisController is managed by AppContext")
};

struct RecommendControllerForeign {
    Q_GADGET
    QML_FOREIGN(linernotes::ui::RecommendController)
    QML_NAMED_ELEMENT(RecommendController)
    QML_UNCREATABLE("RecommendController is managed by AppContext")
};
