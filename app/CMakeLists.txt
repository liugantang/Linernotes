# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

qt_add_executable(linernotes
    main.cpp
)

set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/Main.qml
    PROPERTIES
        QT_RESOURCE_ALIAS Main.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/Theme.qml
    PROPERTIES
        QT_RESOURCE_ALIAS Theme.qml
        QT_QML_SINGLETON_TYPE TRUE
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/NavigationSidebar.qml
    PROPERTIES
        QT_RESOURCE_ALIAS NavigationSidebar.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/PlayerBar.qml
    PROPERTIES
        QT_RESOURCE_ALIAS PlayerBar.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/SidePanel.qml
    PROPERTIES
        QT_RESOURCE_ALIAS SidePanel.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/AlbumGrid.qml
    PROPERTIES
        QT_RESOURCE_ALIAS AlbumGrid.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/AlbumDetail.qml
    PROPERTIES
        QT_RESOURCE_ALIAS AlbumDetail.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/CoverSearchDialog.qml
    PROPERTIES
        QT_RESOURCE_ALIAS CoverSearchDialog.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/ArtistDetail.qml
    PROPERTIES
        QT_RESOURCE_ALIAS ArtistDetail.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/pages/TracksPage.qml
    PROPERTIES
        QT_RESOURCE_ALIAS pages/TracksPage.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/pages/AlbumsPage.qml
    PROPERTIES
        QT_RESOURCE_ALIAS pages/AlbumsPage.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/pages/ArtistsPage.qml
    PROPERTIES
        QT_RESOURCE_ALIAS pages/ArtistsPage.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/pages/PlaylistsPage.qml
    PROPERTIES
        QT_RESOURCE_ALIAS pages/PlaylistsPage.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/pages/CleanupPage.qml
    PROPERTIES
        QT_RESOURCE_ALIAS pages/CleanupPage.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/CleanupHealthPanel.qml
    PROPERTIES
        QT_RESOURCE_ALIAS CleanupHealthPanel.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/CorrectionBatchList.qml
    PROPERTIES
        QT_RESOURCE_ALIAS CorrectionBatchList.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/CorrectionFilterBar.qml
    PROPERTIES
        QT_RESOURCE_ALIAS CorrectionFilterBar.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/CorrectionList.qml
    PROPERTIES
        QT_RESOURCE_ALIAS CorrectionList.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/CorrectionRowItem.qml
    PROPERTIES
        QT_RESOURCE_ALIAS CorrectionRowItem.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/CorrectionActionBar.qml
    PROPERTIES
        QT_RESOURCE_ALIAS CorrectionActionBar.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/DuplicatePanel.qml
    PROPERTIES
        QT_RESOURCE_ALIAS DuplicatePanel.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/pages/SettingsPage.qml
    PROPERTIES
        QT_RESOURCE_ALIAS pages/SettingsPage.qml
)

set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/controls/AppButton.qml
    PROPERTIES
        QT_RESOURCE_ALIAS controls/AppButton.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/controls/IconButton.qml
    PROPERTIES
        QT_RESOURCE_ALIAS controls/IconButton.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/controls/FavoriteButton.qml
    PROPERTIES
        QT_RESOURCE_ALIAS controls/FavoriteButton.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/controls/RatingStars.qml
    PROPERTIES
        QT_RESOURCE_ALIAS controls/RatingStars.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/controls/SeekBar.qml
    PROPERTIES
        QT_RESOURCE_ALIAS controls/SeekBar.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/controls/AppCheckBox.qml
    PROPERTIES
        QT_RESOURCE_ALIAS controls/AppCheckBox.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/controls/AppScrollBar.qml
    PROPERTIES
        QT_RESOURCE_ALIAS controls/AppScrollBar.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/controls/AppComboBox.qml
    PROPERTIES
        QT_RESOURCE_ALIAS controls/AppComboBox.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/controls/AppTabBar.qml
    PROPERTIES
        QT_RESOURCE_ALIAS controls/AppTabBar.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/controls/AppTabButton.qml
    PROPERTIES
        QT_RESOURCE_ALIAS controls/AppTabButton.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/controls/AppMenu.qml
    PROPERTIES
        QT_RESOURCE_ALIAS controls/AppMenu.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/controls/AppMenuItem.qml
    PROPERTIES
        QT_RESOURCE_ALIAS controls/AppMenuItem.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/controls/AppMenuSeparator.qml
    PROPERTIES
        QT_RESOURCE_ALIAS controls/AppMenuSeparator.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/controls/AppTextField.qml
    PROPERTIES
        QT_RESOURCE_ALIAS controls/AppTextField.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/controls/ModelComboField.qml
    PROPERTIES
        QT_RESOURCE_ALIAS controls/ModelComboField.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/controls/AppSpinBox.qml
    PROPERTIES
        QT_RESOURCE_ALIAS controls/AppSpinBox.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/TrackColumns.js
    PROPERTIES
        QT_RESOURCE_ALIAS TrackColumns.js
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/TrackContextMenu.qml
    PROPERTIES
        QT_RESOURCE_ALIAS TrackContextMenu.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/TrackTableHeader.qml
    PROPERTIES
        QT_RESOURCE_ALIAS TrackTableHeader.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/TrackTableRow.qml
    PROPERTIES
        QT_RESOURCE_ALIAS TrackTableRow.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/VersionBadge.qml
    PROPERTIES
        QT_RESOURCE_ALIAS VersionBadge.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/TrackTable.qml
    PROPERTIES
        QT_RESOURCE_ALIAS TrackTable.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/TrackTableReorder.qml
    PROPERTIES
        QT_RESOURCE_ALIAS TrackTableReorder.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/AlbumTrackRow.qml
    PROPERTIES
        QT_RESOURCE_ALIAS AlbumTrackRow.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/PlayerBarTrackInfo.qml
    PROPERTIES
        QT_RESOURCE_ALIAS PlayerBarTrackInfo.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/VolumeControl.qml
    PROPERTIES
        QT_RESOURCE_ALIAS VolumeControl.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/PlaylistNameDialog.qml
    PROPERTIES
        QT_RESOURCE_ALIAS PlaylistNameDialog.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/PlaylistList.qml
    PROPERTIES
        QT_RESOURCE_ALIAS PlaylistList.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/SmartConditionRow.qml
    PROPERTIES
        QT_RESOURCE_ALIAS SmartConditionRow.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/SmartRuleSortRow.qml
    PROPERTIES
        QT_RESOURCE_ALIAS SmartRuleSortRow.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/SmartRuleDialog.qml
    PROPERTIES
        QT_RESOURCE_ALIAS SmartRuleDialog.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/TagEditorDialog.qml
    PROPERTIES
        QT_RESOURCE_ALIAS TagEditorDialog.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/QueueRow.qml
    PROPERTIES
        QT_RESOURCE_ALIAS QueueRow.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/QueuePanel.qml
    PROPERTIES
        QT_RESOURCE_ALIAS QueuePanel.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/CommandPalette.qml
    PROPERTIES
        QT_RESOURCE_ALIAS CommandPalette.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/SimilarTracksDialog.qml
    PROPERTIES
        QT_RESOURCE_ALIAS SimilarTracksDialog.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/SearchBox.qml
    PROPERTIES
        QT_RESOURCE_ALIAS SearchBox.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/SearchResults.qml
    PROPERTIES
        QT_RESOURCE_ALIAS SearchResults.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/SearchSectionArtists.qml
    PROPERTIES
        QT_RESOURCE_ALIAS SearchSectionArtists.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/SearchSectionAlbums.qml
    PROPERTIES
        QT_RESOURCE_ALIAS SearchSectionAlbums.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/SearchSectionTracks.qml
    PROPERTIES
        QT_RESOURCE_ALIAS SearchSectionTracks.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/SettingsLibrarySection.qml
    PROPERTIES
        QT_RESOURCE_ALIAS SettingsLibrarySection.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/SettingsPlaybackSection.qml
    PROPERTIES
        QT_RESOURCE_ALIAS SettingsPlaybackSection.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/SettingsAppearanceSection.qml
    PROPERTIES
        QT_RESOURCE_ALIAS SettingsAppearanceSection.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/SettingsAiSection.qml
    PROPERTIES
        QT_RESOURCE_ALIAS SettingsAiSection.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/SettingsAiServices.qml
    PROPERTIES
        QT_RESOURCE_ALIAS SettingsAiServices.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/SettingsAiServiceDialog.qml
    PROPERTIES
        QT_RESOURCE_ALIAS SettingsAiServiceDialog.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/SettingsAiRoutes.qml
    PROPERTIES
        QT_RESOURCE_ALIAS SettingsAiRoutes.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/SettingsAiPrivacy.qml
    PROPERTIES
        QT_RESOURCE_ALIAS SettingsAiPrivacy.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/SettingsAiUsage.qml
    PROPERTIES
        QT_RESOURCE_ALIAS SettingsAiUsage.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/SettingsAiDeveloper.qml
    PROPERTIES
        QT_RESOURCE_ALIAS SettingsAiDeveloper.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/LlmDebugPanel.qml
    PROPERTIES
        QT_RESOURCE_ALIAS LlmDebugPanel.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/AppShortcuts.qml
    PROPERTIES
        QT_RESOURCE_ALIAS AppShortcuts.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/ShortcutsDialog.qml
    PROPERTIES
        QT_RESOURCE_ALIAS ShortcutsDialog.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/FirstRunWizard.qml
    PROPERTIES
        QT_RESOURCE_ALIAS FirstRunWizard.qml
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/qml/TrayIcon.qml
    PROPERTIES
        QT_RESOURCE_ALIAS TrayIcon.qml
)

set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/play.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/play.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/pause.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/pause.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/skip-back.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/skip-back.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/skip-forward.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/skip-forward.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/shuffle.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/shuffle.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/panel-right-close.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/panel-right-close.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/panel-right-open.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/panel-right-open.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/arrow-left.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/arrow-left.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/arrow-up-narrow-wide.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/arrow-up-narrow-wide.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/arrow-down-wide-narrow.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/arrow-down-wide-narrow.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/disc-3.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/disc-3.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/mic-vocal.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/mic-vocal.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/list-music.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/list-music.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/sparkles.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/sparkles.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/library.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/library.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/arrow-right.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/arrow-right.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/music.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/music.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/repeat.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/repeat.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/repeat-1.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/repeat-1.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/volume-x.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/volume-x.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/volume-1.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/volume-1.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/volume-2.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/volume-2.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/list-plus.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/list-plus.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/trash-2.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/trash-2.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/x.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/x.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/search.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/search.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/settings.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/settings.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/grip-vertical.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/grip-vertical.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/audio-lines.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/audio-lines.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/heart.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/heart.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/heart-filled.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/heart-filled.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/star.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/star.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/star-filled.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/star-filled.svg
)
set_source_files_properties(
    ${CMAKE_SOURCE_DIR}/resources/icons/rotate-ccw.svg
    PROPERTIES
        QT_RESOURCE_ALIAS icons/rotate-ccw.svg
)

qt_add_qml_module(linernotes
    URI Linernotes
    VERSION 1.0
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES
        ${CMAKE_SOURCE_DIR}/qml/controls/AppButton.qml
        ${CMAKE_SOURCE_DIR}/qml/controls/AppCheckBox.qml
        ${CMAKE_SOURCE_DIR}/qml/controls/AppScrollBar.qml
        ${CMAKE_SOURCE_DIR}/qml/controls/IconButton.qml
        ${CMAKE_SOURCE_DIR}/qml/controls/FavoriteButton.qml
        ${CMAKE_SOURCE_DIR}/qml/controls/RatingStars.qml
        ${CMAKE_SOURCE_DIR}/qml/controls/SeekBar.qml
        ${CMAKE_SOURCE_DIR}/qml/controls/AppComboBox.qml
        ${CMAKE_SOURCE_DIR}/qml/controls/AppTabBar.qml
        ${CMAKE_SOURCE_DIR}/qml/controls/AppTabButton.qml
        ${CMAKE_SOURCE_DIR}/qml/controls/AppMenu.qml
        ${CMAKE_SOURCE_DIR}/qml/controls/AppMenuItem.qml
        ${CMAKE_SOURCE_DIR}/qml/controls/AppMenuSeparator.qml
        ${CMAKE_SOURCE_DIR}/qml/controls/AppTextField.qml
        ${CMAKE_SOURCE_DIR}/qml/controls/ModelComboField.qml
        ${CMAKE_SOURCE_DIR}/qml/controls/AppSpinBox.qml
        ${CMAKE_SOURCE_DIR}/qml/Main.qml
        ${CMAKE_SOURCE_DIR}/qml/Theme.qml
        ${CMAKE_SOURCE_DIR}/qml/NavigationSidebar.qml
        ${CMAKE_SOURCE_DIR}/qml/PlayerBar.qml
        ${CMAKE_SOURCE_DIR}/qml/PlayerBarTrackInfo.qml
        ${CMAKE_SOURCE_DIR}/qml/VolumeControl.qml
        ${CMAKE_SOURCE_DIR}/qml/SidePanel.qml
        ${CMAKE_SOURCE_DIR}/qml/QueuePanel.qml
        ${CMAKE_SOURCE_DIR}/qml/QueueRow.qml
        ${CMAKE_SOURCE_DIR}/qml/PlaylistNameDialog.qml
        ${CMAKE_SOURCE_DIR}/qml/PlaylistList.qml
        ${CMAKE_SOURCE_DIR}/qml/SmartConditionRow.qml
        ${CMAKE_SOURCE_DIR}/qml/SmartRuleSortRow.qml
        ${CMAKE_SOURCE_DIR}/qml/SmartRuleDialog.qml
        ${CMAKE_SOURCE_DIR}/qml/TagEditorDialog.qml
        ${CMAKE_SOURCE_DIR}/qml/AlbumGrid.qml
        ${CMAKE_SOURCE_DIR}/qml/AlbumDetail.qml
        ${CMAKE_SOURCE_DIR}/qml/CoverSearchDialog.qml
        ${CMAKE_SOURCE_DIR}/qml/AlbumTrackRow.qml
        ${CMAKE_SOURCE_DIR}/qml/ArtistDetail.qml
        ${CMAKE_SOURCE_DIR}/qml/CommandPalette.qml
        ${CMAKE_SOURCE_DIR}/qml/SimilarTracksDialog.qml
        ${CMAKE_SOURCE_DIR}/qml/SearchBox.qml
        ${CMAKE_SOURCE_DIR}/qml/SearchResults.qml
        ${CMAKE_SOURCE_DIR}/qml/SearchSectionArtists.qml
        ${CMAKE_SOURCE_DIR}/qml/SearchSectionAlbums.qml
        ${CMAKE_SOURCE_DIR}/qml/SearchSectionTracks.qml
        ${CMAKE_SOURCE_DIR}/qml/SettingsLibrarySection.qml
        ${CMAKE_SOURCE_DIR}/qml/SettingsPlaybackSection.qml
        ${CMAKE_SOURCE_DIR}/qml/SettingsAppearanceSection.qml
        ${CMAKE_SOURCE_DIR}/qml/SettingsAiSection.qml
        ${CMAKE_SOURCE_DIR}/qml/SettingsAiServices.qml
        ${CMAKE_SOURCE_DIR}/qml/SettingsAiServiceDialog.qml
        ${CMAKE_SOURCE_DIR}/qml/SettingsAiRoutes.qml
        ${CMAKE_SOURCE_DIR}/qml/SettingsAiPrivacy.qml
        ${CMAKE_SOURCE_DIR}/qml/SettingsAiUsage.qml
        ${CMAKE_SOURCE_DIR}/qml/SettingsAiDeveloper.qml
        ${CMAKE_SOURCE_DIR}/qml/LlmDebugPanel.qml
        ${CMAKE_SOURCE_DIR}/qml/AppShortcuts.qml
        ${CMAKE_SOURCE_DIR}/qml/ShortcutsDialog.qml
        ${CMAKE_SOURCE_DIR}/qml/FirstRunWizard.qml
        ${CMAKE_SOURCE_DIR}/qml/TrayIcon.qml
        ${CMAKE_SOURCE_DIR}/qml/TrackColumns.js
        ${CMAKE_SOURCE_DIR}/qml/TrackContextMenu.qml
        ${CMAKE_SOURCE_DIR}/qml/TrackTableHeader.qml
        ${CMAKE_SOURCE_DIR}/qml/TrackTableRow.qml
        ${CMAKE_SOURCE_DIR}/qml/VersionBadge.qml
        ${CMAKE_SOURCE_DIR}/qml/TrackTable.qml
        ${CMAKE_SOURCE_DIR}/qml/TrackTableReorder.qml
        ${CMAKE_SOURCE_DIR}/qml/CorrectionBatchList.qml
        ${CMAKE_SOURCE_DIR}/qml/CorrectionFilterBar.qml
        ${CMAKE_SOURCE_DIR}/qml/CorrectionList.qml
        ${CMAKE_SOURCE_DIR}/qml/CorrectionRowItem.qml
        ${CMAKE_SOURCE_DIR}/qml/CorrectionActionBar.qml
        ${CMAKE_SOURCE_DIR}/qml/DuplicatePanel.qml
        ${CMAKE_SOURCE_DIR}/qml/CleanupHealthPanel.qml
        ${CMAKE_SOURCE_DIR}/qml/pages/TracksPage.qml
        ${CMAKE_SOURCE_DIR}/qml/pages/AlbumsPage.qml
        ${CMAKE_SOURCE_DIR}/qml/pages/ArtistsPage.qml
        ${CMAKE_SOURCE_DIR}/qml/pages/PlaylistsPage.qml
        ${CMAKE_SOURCE_DIR}/qml/pages/CleanupPage.qml
        ${CMAKE_SOURCE_DIR}/qml/pages/SettingsPage.qml
    RESOURCES
        ${CMAKE_SOURCE_DIR}/resources/icons/play.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/pause.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/skip-back.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/skip-forward.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/shuffle.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/panel-right-close.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/panel-right-open.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/arrow-left.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/arrow-right.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/arrow-up-narrow-wide.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/arrow-down-wide-narrow.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/disc-3.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/mic-vocal.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/music.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/list-music.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/list-plus.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/repeat.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/repeat-1.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/rotate-ccw.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/search.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/settings.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/sparkles.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/library.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/heart.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/heart-filled.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/star.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/star-filled.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/trash-2.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/volume-x.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/volume-1.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/volume-2.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/x.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/grip-vertical.svg
        ${CMAKE_SOURCE_DIR}/resources/icons/audio-lines.svg
    SOURCES
        AppInfo.cpp
        AppInfo.h
        QmlTypes.cpp
        QmlTypes.h
)

target_link_libraries(linernotes
    PRIVATE
        Linernotes::core
        Linernotes::library
        Linernotes::player
        Linernotes::ui
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Widgets
)

qt_add_translations(linernotes
    TS_FILE_BASE linernotes
    TS_FILE_DIR ${CMAKE_SOURCE_DIR}/i18n
    RESOURCE_PREFIX /i18n
    SOURCE_TARGETS linernotes linernotes_ui linernotes_butler
)

linernotes_set_target_options(linernotes)

install(TARGETS linernotes
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

set(LINERNOTES_AUDIO_MODEL "" CACHE FILEPATH "Path to msclap audio embedding ONNX model")
if(LINERNOTES_AUDIO_MODEL)
    add_custom_command(TARGET linernotes POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:linernotes>/models
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LINERNOTES_AUDIO_MODEL}" $<TARGET_FILE_DIR:linernotes>/models/msclap-2023-audio.onnx
        COMMENT "Copying audio embedding model to app directory"
    )
    install(FILES "${LINERNOTES_AUDIO_MODEL}"
        DESTINATION ${CMAKE_INSTALL_BINDIR}/models
        RENAME msclap-2023-audio.onnx
    )
endif()
