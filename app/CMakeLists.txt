add_executable(aimusic
    main.cpp
)

target_link_libraries(aimusic
    PRIVATE
        AiMusic::core
)
