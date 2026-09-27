#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

# ==============================================================================
# Linernotes AppImage Packaging Script
#
# Usage:
#   ./packaging/appimage/build-appimage.sh
#
# Requirements:
#   - Build tools: cmake, ninja, C++20 compiler (GCC/Clang), git
#   - Development libraries: Qt 6 (Core, Gui, Quick, QuickControls2, Widgets, Sql,
#     Network, DBus, Concurrent, LinguistTools, Svg), QtKeyChain, libmpv, TagLib,
#     uchardet, ICU, chromaprint, FFmpeg, libebur128, SQLite3
#   - Network access (curl/wget) to download linuxdeploy tools if not already in PATH
#
# Notes on dependencies:
#   - libmpv and third-party shared libraries are automatically gathered into
#     the AppImage bundle by linuxdeploy.
#   - Host graphics and hardware video acceleration drivers (e.g., Mesa, VA-API,
#     VDPAU, Vulkan drivers) are not bundled inside the AppImage by design;
#     they are loaded dynamically from the host operating system at runtime.
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

BUILD_DIR="${REPO_ROOT}/build/appimage-build"
TOOLS_DIR="${REPO_ROOT}/build/appimage-tools"
OUTPUT_DIR="${REPO_ROOT}/build"
APP_DIR="${BUILD_DIR}/AppDir"

mkdir -p "${TOOLS_DIR}"
mkdir -p "${OUTPUT_DIR}"

# 1. Determine Version
# 仓库的 tag 是阶段标记（phase-NN），版本号取 CMake project(VERSION) 加提交哈希
VERSION="$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' "${REPO_ROOT}/CMakeLists.txt" | head -n1)"
VERSION="${VERSION}-g$(git -C "${REPO_ROOT}" rev-parse --short=7 HEAD)"
export VERSION

echo "==> Packaging Linernotes version: ${VERSION}"

# 2. Locate or download linuxdeploy and linuxdeploy-plugin-qt
LINUXDEPLOY=""
LINUXDEPLOY_QT_PLUGIN=""

if command -v linuxdeploy-x86_64.AppImage >/dev/null 2>&1; then
    LINUXDEPLOY="$(command -v linuxdeploy-x86_64.AppImage)"
elif command -v linuxdeploy >/dev/null 2>&1; then
    LINUXDEPLOY="$(command -v linuxdeploy)"
fi

if command -v linuxdeploy-plugin-qt-x86_64.AppImage >/dev/null 2>&1; then
    LINUXDEPLOY_QT_PLUGIN="$(command -v linuxdeploy-plugin-qt-x86_64.AppImage)"
elif command -v linuxdeploy-plugin-qt >/dev/null 2>&1; then
    LINUXDEPLOY_QT_PLUGIN="$(command -v linuxdeploy-plugin-qt)"
fi

if [ -z "${LINUXDEPLOY}" ]; then
    LINUXDEPLOY="${TOOLS_DIR}/linuxdeploy-x86_64.AppImage"
    if [ ! -f "${LINUXDEPLOY}" ]; then
        echo "==> Downloading linuxdeploy..."
        curl -fsSL "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" -o "${LINUXDEPLOY}"
        chmod +x "${LINUXDEPLOY}"
    fi
fi

if [ -z "${LINUXDEPLOY_QT_PLUGIN}" ]; then
    LINUXDEPLOY_QT_PLUGIN="${TOOLS_DIR}/linuxdeploy-plugin-qt-x86_64.AppImage"
    if [ ! -f "${LINUXDEPLOY_QT_PLUGIN}" ]; then
        echo "==> Downloading linuxdeploy-plugin-qt..."
        curl -fsSL "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage" -o "${LINUXDEPLOY_QT_PLUGIN}"
        chmod +x "${LINUXDEPLOY_QT_PLUGIN}"
    fi
fi

# Ensure tools directory is in PATH so linuxdeploy can find the qt plugin
export PATH="${TOOLS_DIR}:${PATH}"
export APPIMAGE_EXTRACT_AND_RUN=1
# linuxdeploy 自带的 strip 不认识新 binutils 生成的 .relr.dyn 节，会直接失败
export NO_STRIP=1

# 3. Build Linernotes (Release mode)
echo "==> Configuring and building Linernotes (Release)..."
cmake -B "${BUILD_DIR}" -S "${REPO_ROOT}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DLINERNOTES_BUILD_TESTS=OFF \
    -DLINERNOTES_BUILD_TOOLS=OFF

cmake --build "${BUILD_DIR}"

# 4. Install into AppDir
echo "==> Installing to AppDir..."
rm -rf "${APP_DIR}"
DESTDIR="${APP_DIR}" cmake --install "${BUILD_DIR}"

# 5. Package with linuxdeploy and qt plugin
echo "==> Generating AppImage with linuxdeploy..."
# 有的发行版 qmake 指向 Qt 5，插件会按 Qt 5 部署；优先用 qmake6
QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
export QMAKE
export QML_SOURCES_PATHS="${REPO_ROOT}/qml"
# 项目自己的 QML 模块编译进了程序，但 qmlimportscanner 仍要求能找到它的 qmldir
export QML_MODULES_PATHS="${BUILD_DIR}/app"
# 只用 SQLite；其他 SQL 驱动的客户端库（如 Firebird 的 libfbclient）多半没装
export LINUXDEPLOY_EXCLUDED_LIBRARIES="libqsqlibase.so;libqsqlmysql.so;libqsqlodbc.so;libqsqlpsql.so;libqsqlmimer.so;libqsqloci.so"
export EXTRA_QT_MODULES="svg;dbus"
export EXTRA_PLATFORM_PLUGINS="libqwayland.so"

TARGET_APPIMAGE="${OUTPUT_DIR}/Linernotes-${VERSION}-x86_64.AppImage"

"${LINUXDEPLOY}" \
    --appdir "${APP_DIR}" \
    --desktop-file "${APP_DIR}/usr/share/applications/linernotes.desktop" \
    --icon-file "${APP_DIR}/usr/share/icons/hicolor/scalable/apps/linernotes.svg" \
    --plugin qt

# Wayland 下创建 OpenGL 上下文需要 wayland-egl 等缓冲区集成插件，Qt 插件不会部署它们
QT_PLUGINS_DIR="$("${QMAKE}" -query QT_INSTALL_PLUGINS)"
WAYLAND_GFX="${QT_PLUGINS_DIR}/wayland-graphics-integration-client"
if [ -d "${WAYLAND_GFX}" ]; then
    cp -r "${WAYLAND_GFX}" "${APP_DIR}/usr/plugins/"
    for lib in "${APP_DIR}/usr/plugins/wayland-graphics-integration-client/"*.so; do
        "${LINUXDEPLOY}" --appdir "${APP_DIR}" --deploy-deps-only "${lib}"
    done
fi

(
    cd "${OUTPUT_DIR}"
    "${LINUXDEPLOY}" --appdir "${APP_DIR}" --output appimage
)

# Standardize output name if needed
if [ ! -f "${TARGET_APPIMAGE}" ]; then
    GENERATED_APPIMAGE="$(find "${OUTPUT_DIR}" -maxdepth 1 -name "Linernotes*.AppImage" -print -quit)"
    if [ -n "${GENERATED_APPIMAGE}" ] && [ "${GENERATED_APPIMAGE}" != "${TARGET_APPIMAGE}" ]; then
        mv "${GENERATED_APPIMAGE}" "${TARGET_APPIMAGE}"
    fi
fi

echo "==> AppImage created successfully: ${TARGET_APPIMAGE}"
