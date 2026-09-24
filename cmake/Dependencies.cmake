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

# Aliases for third-party dependencies
add_library(aimusic_dep_mpv INTERFACE)
target_link_libraries(aimusic_dep_mpv INTERFACE PkgConfig::MPV)
add_library(AiMusic::Deps::Mpv ALIAS aimusic_dep_mpv)

add_library(aimusic_dep_taglib INTERFACE)
target_link_libraries(aimusic_dep_taglib INTERFACE PkgConfig::TAGLIB)
add_library(AiMusic::Deps::TagLib ALIAS aimusic_dep_taglib)

add_library(aimusic_dep_uchardet INTERFACE)
target_link_libraries(aimusic_dep_uchardet INTERFACE PkgConfig::UCHARDET)
add_library(AiMusic::Deps::Uchardet ALIAS aimusic_dep_uchardet)

add_library(aimusic_dep_icu INTERFACE)
target_link_libraries(aimusic_dep_icu INTERFACE PkgConfig::ICU)
add_library(AiMusic::Deps::ICU ALIAS aimusic_dep_icu)

add_library(aimusic_dep_chromaprint INTERFACE)
target_link_libraries(aimusic_dep_chromaprint INTERFACE PkgConfig::CHROMAPRINT)
add_library(AiMusic::Deps::Chromaprint ALIAS aimusic_dep_chromaprint)

add_library(aimusic_dep_ffmpeg INTERFACE)
target_link_libraries(aimusic_dep_ffmpeg INTERFACE PkgConfig::FFMPEG)
add_library(AiMusic::Deps::FFmpeg ALIAS aimusic_dep_ffmpeg)

add_library(aimusic_dep_ebur128 INTERFACE)
target_link_libraries(aimusic_dep_ebur128 INTERFACE PkgConfig::EBUR128)
add_library(AiMusic::Deps::Ebur128 ALIAS aimusic_dep_ebur128)

add_library(aimusic_dep_keychain INTERFACE)
target_link_libraries(aimusic_dep_keychain INTERFACE Qt6Keychain::Qt6Keychain)
find_path(QTKEYCHAIN_INCLUDE_DIR
    NAMES keychain.h
    PATH_SUFFIXES qt6keychain qt5keychain
)
if(QTKEYCHAIN_INCLUDE_DIR)
    target_include_directories(aimusic_dep_keychain INTERFACE "${QTKEYCHAIN_INCLUDE_DIR}")
endif()
add_library(AiMusic::Deps::Keychain ALIAS aimusic_dep_keychain)

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
message(STATUS "================ AiMusic Dependencies Summary ================")
message(STATUS "  Qt6                  : ${Qt6_VERSION}")
message(STATUS "  Qt6Keychain          : ${Qt6Keychain_VERSION}")
message(STATUS "  mpv                  : ${MPV_VERSION}")
message(STATUS "  taglib               : ${TAGLIB_VERSION}")
message(STATUS "  uchardet             : ${UCHARDET_VERSION}")
message(STATUS "  icu                  : ${ICU_VERSION}")
message(STATUS "  libchromaprint       : ${CHROMAPRINT_VERSION}")
message(STATUS "  ffmpeg               : ${FFMPEG_VERSION}")
message(STATUS "  libebur128           : ${EBUR128_VERSION}")
message(STATUS "==============================================================")
