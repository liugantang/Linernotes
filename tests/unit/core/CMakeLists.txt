aimusic_add_test(
    NAME core_Version
    SOURCES
        tst_Version.cpp
    LIBS
        AiMusic::core
)

aimusic_add_test(
    NAME core_Logging
    SOURCES
        tst_Logging.cpp
    LIBS
        AiMusic::core
)
