# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Linernotes contributors

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core
    Gui
    Quick
    QuickControls2
    Sql
    Network
    DBus
    Concurrent
    Test
)

find_package(Qt6Keychain REQUIRED)

find_package(PkgConfig REQUIRED)

pkg_check_modules(MPV REQUIRED IMPORTED_TARGET "mpv>=2.0")
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET "taglib>=2.0")
pkg_check_modules(UCHARDET REQUIRED IMPORTED_TARGET "uchardet")
pkg_check_modules(ICU REQUIRED IMPORTED_TARGET "icu-uc>=70" "icu-i18n>=70")
pkg_check_modules(CHROMAPRINT REQUIRED IMPORTED_TARGET "libchromaprint>=1.5")
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET "libavformat" "libavcodec" "libavutil" "libswresample")
pkg_check_modules(EBUR128 REQUIRED IMPORTED_TARGET "libebur128>=1.2")
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET "sqlite3>=3.40")

# Aliases for third-party dependencies
add_library(linernotes_dep_mpv INTERFACE)
target_link_libraries(linernotes_dep_mpv INTERFACE PkgConfig::MPV)
add_library(Linernotes::Deps::Mpv ALIAS linernotes_dep_mpv)

add_library(linernotes_dep_taglib INTERFACE)
target_link_libraries(linernotes_dep_taglib INTERFACE PkgConfig::TAGLIB)
add_library(Linernotes::Deps::TagLib ALIAS linernotes_dep_taglib)

add_library(linernotes_dep_uchardet INTERFACE)
target_link_libraries(linernotes_dep_uchardet INTERFACE PkgConfig::UCHARDET)
add_library(Linernotes::Deps::Uchardet ALIAS linernotes_dep_uchardet)

add_library(linernotes_dep_icu INTERFACE)
target_link_libraries(linernotes_dep_icu INTERFACE PkgConfig::ICU)
add_library(Linernotes::Deps::ICU ALIAS linernotes_dep_icu)

add_library(linernotes_dep_chromaprint INTERFACE)
target_link_libraries(linernotes_dep_chromaprint INTERFACE PkgConfig::CHROMAPRINT)
add_library(Linernotes::Deps::Chromaprint ALIAS linernotes_dep_chromaprint)

add_library(linernotes_dep_ffmpeg INTERFACE)
target_link_libraries(linernotes_dep_ffmpeg INTERFACE PkgConfig::FFMPEG)
add_library(Linernotes::Deps::FFmpeg ALIAS linernotes_dep_ffmpeg)

add_library(linernotes_dep_ebur128 INTERFACE)
target_link_libraries(linernotes_dep_ebur128 INTERFACE PkgConfig::EBUR128)
add_library(Linernotes::Deps::Ebur128 ALIAS linernotes_dep_ebur128)

add_library(linernotes_dep_keychain INTERFACE)
target_link_libraries(linernotes_dep_keychain INTERFACE Qt6Keychain::Qt6Keychain)
find_path(QTKEYCHAIN_INCLUDE_DIR
    NAMES keychain.h
    PATH_SUFFIXES qt6keychain qt5keychain
)
if(QTKEYCHAIN_INCLUDE_DIR)
    target_include_directories(linernotes_dep_keychain SYSTEM INTERFACE "${QTKEYCHAIN_INCLUDE_DIR}")
endif()
add_library(Linernotes::Deps::Keychain ALIAS linernotes_dep_keychain)

add_library(linernotes_dep_sqlite3 INTERFACE)
target_link_libraries(linernotes_dep_sqlite3 INTERFACE PkgConfig::SQLITE3)
add_library(Linernotes::Deps::SQLite3 ALIAS linernotes_dep_sqlite3)

# Format ICU and FFmpeg versions for display
if(NOT ICU_VERSION)
    if(ICU_icu-uc_VERSION)
        set(ICU_VERSION "${ICU_icu-uc_VERSION}")
    elseif(ICU_icu-i18n_VERSION)
        set(ICU_VERSION "${ICU_icu-i18n_VERSION}")
    endif()
endif()

if(NOT FFMPEG_VERSION)
    if(FFMPEG_libavformat_VERSION)
        set(FFMPEG_VERSION "${FFMPEG_libavformat_VERSION} (avformat), ${FFMPEG_libavcodec_VERSION} (avcodec)")
    endif()
endif()

# Summary table
message(STATUS "================ Linernotes Dependencies Summary ================")
message(STATUS "  Qt6                  : ${Qt6_VERSION}")
message(STATUS "  Qt6Keychain          : ${Qt6Keychain_VERSION}")
message(STATUS "  mpv                  : ${MPV_VERSION}")
message(STATUS "  taglib               : ${TAGLIB_VERSION}")
message(STATUS "  uchardet             : ${UCHARDET_VERSION}")
message(STATUS "  icu                  : ${ICU_VERSION}")
message(STATUS "  libchromaprint       : ${CHROMAPRINT_VERSION}")
message(STATUS "  ffmpeg               : ${FFMPEG_VERSION}")
message(STATUS "  libebur128           : ${EBUR128_VERSION}")
message(STATUS "  sqlite3              : ${SQLITE3_VERSION}")
message(STATUS "==============================================================")
