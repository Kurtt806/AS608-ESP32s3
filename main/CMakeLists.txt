file(GLOB SOUND_FILES "audio/sounds/*.mp3")

# Web assets - includes new ws.js and api.js
set(WEB_ASSETS
    "webserver/assets/index.html"
    "webserver/assets/style.css"
    "webserver/assets/app.js"
    "webserver/assets/ws.js"
    "webserver/assets/api.js"
)

idf_component_register(
    SRCS 
        "main.c"
        "app/app.c"
        "finger/finger.c"
        "finger/finger_meta.c"
        "button/button.c"
        "audio/audio.c"
        "wifi/wifi.cc"
        "webserver/webserver.c"
        "settings/settings.c"
        "ota/ota.c"
    INCLUDE_DIRS 
        "."
        "common"
        "app"
        "finger"
        "button"
        "audio"
        "wifi"
        "webserver"
        "settings"
        "ota"
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
        app_update
        esp_https_ota
    EMBED_FILES ${SOUND_FILES} ${WEB_ASSETS}
)