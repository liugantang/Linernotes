pragma Singleton
// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 AiMusic contributors

import QtQuick

QtObject {
    id: root

    readonly property bool isDark: Application.styleHints.colorScheme === Qt.ColorScheme.Dark

    // Colors
    readonly property color background: isDark ? "#1e1e2e" : "#f8f9fa"
    readonly property color surface: isDark ? "#2a2a3c" : "#ffffff"
    readonly property color text: isDark ? "#cdd6f4" : "#1e1e2e"
    readonly property color textSecondary: isDark ? "#a6adc8" : "#6c757d"
    readonly property color accent: isDark ? "#89b4fa" : "#0066cc"

    // Font sizes
    readonly property int fontSizeSmall: 12
    readonly property int fontSizeNormal: 14
    readonly property int fontSizeLarge: 18
    readonly property int fontSizeTitle: 28

    // Spacing
    readonly property int spacingSmall: 8
    readonly property int spacingMedium: 16
    readonly property int spacingLarge: 24
    readonly property int spacingExtraLarge: 32

    // Window dimensions
    readonly property int windowDefaultWidth: 1200
    readonly property int windowDefaultHeight: 800
    readonly property int windowMinWidth: 800
    readonly property int windowMinHeight: 500
}
