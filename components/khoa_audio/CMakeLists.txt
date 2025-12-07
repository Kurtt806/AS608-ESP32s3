file(GLOB SOUND_FILES "sounds/*.mp3")

idf_component_register(
    SRCS "audio.cpp"
    INCLUDE_DIRS "include"
    REQUIRES 
        chmorgan__esp-libhelix-mp3
        esp_event
        driver
        freertos
    EMBED_FILES ${SOUND_FILES}
)