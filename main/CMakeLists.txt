file(GLOB SOUND_FILES "audio/sounds/*.mp3")

# Web assets
set(WEB_ASSETS
    "webserver/assets/index.html"
    "webserver/assets/style.css"
    "webserver/assets/app.js"
)

idf_component_register(
    SRCS 
        "main.c"
        "app/app.c"
        "finger/finger.c"
        "button/button.c"
        "audio/audio.c"
        "wifi/wifi.cc"
        "webserver/webserver.c"
    INCLUDE_DIRS 
        "."
        "common"
        "app"
        "finger"
        "button"
        "audio"
        "wifi"
        "webserver"
    REQUIRES 
        as608 
        nvs_flash 
        esp_event
        espressif__button
        driver
        esp_adc
        khoa_esp_wifi
        esp_http_server
        json
    EMBED_FILES ${SOUND_FILES} ${WEB_ASSETS}
)