# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

add_library(linernotes_ui STATIC
    AiContext.cpp
    AiContext.h
    AiSettingsController.cpp
    AiSettingsController.h
    AlbumGridModel.cpp
    AlbumGridModel.h
    AppContext.cpp
    AppContext.h
    AppSettings.h
    ArtistListModel.cpp
    ArtistListModel.h
    AudioAnalysisController.cpp
    AudioAnalysisController.h
    CleanupController.cpp
    CleanupController.h
    CorrectionBatchModel.cpp
    CorrectionBatchModel.h
    CorrectionListModel.cpp
    CorrectionListModel.h
    CorrectionReviewController.cpp
    CorrectionReviewController.h
    CoverAccent.cpp
    CoverAccent.h
    CoverImageProvider.cpp
    CoverImageProvider.h
    CoverSearchController.cpp
    CoverSearchController.h
    DuplicateController.cpp
    DuplicateController.h
    ErrorText.cpp
    ErrorText.h
    Format.h
    LibraryActions.cpp
    LibraryActions.h
    LibraryRootsModel.cpp
    LibraryRootsModel.h
    LlmDebugController.cpp
    LlmDebugController.h
    LlmDebugModel.cpp
    LlmDebugModel.h
    MarksController.cpp
    MarksController.h
    NlqController.cpp
    NlqController.h
    NotificationSink.h
    NowPlaying.cpp
    NowPlaying.h
    PageCache.h
    PagedListModel.cpp
    PagedListModel.h
    PlayEventRecorder.cpp
    PlayEventRecorder.h
    PlaylistController.cpp
    PlaylistController.h
    PlaylistListModel.cpp
    PlaylistListModel.h
    QueueModel.cpp
    QueueModel.h
    RowSelection.cpp
    RowSelection.h
    SearchController.cpp
    SearchController.h
    ServiceListModel.cpp
    ServiceListModel.h
    SettingsController.cpp
    SettingsController.h
    SmartLabels.cpp
    SmartLabels.h
    TagEditorModel.cpp
    TagEditorModel.h
    TrackListModel.cpp
    TrackListModel.h
    TrackNotifier.cpp
    TrackNotifier.h
    Translations.cpp
    Translations.h
    UiLogging.cpp
    UiLogging.h
    UsageSummaryModel.cpp
    UsageSummaryModel.h
    WritebackController.cpp
    WritebackController.h
)
add_library(Linernotes::ui ALIAS linernotes_ui)

if(UNIX AND NOT APPLE)
    target_sources(linernotes_ui PRIVATE
        DBusNotificationSink.cpp
        DBusNotificationSink.h
        mpris/Mpris.cpp
        mpris/Mpris.h
        mpris/MprisPlayerAdaptor.cpp
        mpris/MprisPlayerAdaptor.h
        mpris/MprisRootAdaptor.cpp
        mpris/MprisRootAdaptor.h
    )
    target_link_libraries(linernotes_ui
        PUBLIC
            Qt6::DBus
    )
endif()

qt6_extract_metatypes(linernotes_ui)

target_include_directories(linernotes_ui
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(linernotes_ui
    PUBLIC
        Linernotes::ai
        Linernotes::audio
        Linernotes::butler
        Linernotes::core
        Linernotes::library
        Linernotes::nlq
        Linernotes::player
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Quick # Required by CoverImageProvider (QQuickImageProvider)
        Qt6::Concurrent
)

linernotes_set_target_options(linernotes_ui)
