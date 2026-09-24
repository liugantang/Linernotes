configure_file(
    Version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/core/Version.h
    @ONLY
)

add_library(aimusic_core STATIC
    Logging.cpp
    Logging.h
    Version.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/generated/core/Version.h
)
add_library(AiMusic::core ALIAS aimusic_core)

target_include_directories(aimusic_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(aimusic_core
    PUBLIC
        Qt6::Core
)

aimusic_set_target_options(aimusic_core)
