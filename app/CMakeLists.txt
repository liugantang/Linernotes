add_executable(aimusic
    main.cpp
)

target_link_libraries(aimusic
    PRIVATE
        AiMusic::core
)

aimusic_set_target_options(aimusic)
