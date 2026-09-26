pragma Singleton
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Linernotes contributors

import QtQuick

QtObject {
    id: root

    readonly property bool isDark: Application.styleHints.colorScheme === Qt.ColorScheme.Dark

    // Colors
    readonly property color background: isDark ? "#1e1e2e" : "#f8f9fa"
    readonly property color surface: isDark ? "#2a2a3c" : "#ffffff"
    readonly property color surfaceVariant: isDark ? "#313244" : "#eef0f3"
    readonly property color text: isDark ? "#cdd6f4" : "#1e1e2e"
    readonly property color textSecondary: isDark ? "#a6adc8" : "#6c757d"
    readonly property color accent: isDark ? "#89b4fa" : "#0066cc"
    readonly property color accentHover: isDark ? "#b4befe" : "#0052a3"
    readonly property color divider: isDark ? "#313244" : "#dee2e6"
    readonly property color itemHover: isDark ? "#313244" : "#e9ecef"
    readonly property color itemSelected: isDark ? "#45475a" : "#dee2e6"

    // Error banner colors
    readonly property color errorBackground: isDark ? "#3b1e24" : "#fde8e8"
    readonly property color errorBorder: isDark ? "#6e2a34" : "#f8b4b4"
    readonly property color errorText: isDark ? "#f38ba8" : "#9b1c1c"

    // Font sizes
    readonly property int fontSizeSmall: 12
    readonly property int fontSizeNormal: 14
    readonly property int fontSizeLarge: 18
    readonly property int fontSizeTitle: 28

    // Spacing
    readonly property int spacingTiny: 4
    readonly property int spacingSmall: 8
    readonly property int spacingMedium: 16
    readonly property int spacingLarge: 24
    readonly property int spacingExtraLarge: 32

    readonly property int radiusSmall: 4
    readonly property int radiusMedium: 6
    readonly property int controlHeight: 32
    readonly property int iconSize: 16
    readonly property color accentText: "#ffffff"
    readonly property color hoverOverlay: isDark ? Qt.rgba(1, 1, 1, 0.1) : Qt.rgba(0, 0, 0, 0.05)
    readonly property color focusRing: isDark ? "#6689b4fa" : "#4d0066cc"

    // Window dimensions
    readonly property int windowDefaultWidth: 1200
    readonly property int windowDefaultHeight: 800
    readonly property int windowMinWidth: 800
    readonly property int windowMinHeight: 500

    // Component dimensions
    readonly property int sidebarWidth: 200
    readonly property int navItemHeight: 40
    readonly property int navItemHeightWithSubtitle: 56
    readonly property int iconSizeSmall: 20
    readonly property int playerBarHeight: 64
    readonly property int sidePanelWidth: 280
    readonly property int topBarHeight: 40

    // Album & Artist view dimensions
    readonly property int albumCardMinWidth: 160
    readonly property int albumCardTextHeight: 52
    readonly property int albumDetailCoverSize: 180
    readonly property int artistAvatarSizeSmall: 44
    readonly property int artistAvatarSizeLarge: 96
    readonly property int artistListWidth: 260
    readonly property int coverBorderRadius: 6
    readonly property int cardBorderRadius: 8
    readonly property int trackRowHeight: 44
    readonly property int menuMinWidth: 220
    readonly property int tableRowHeight: 32
}
