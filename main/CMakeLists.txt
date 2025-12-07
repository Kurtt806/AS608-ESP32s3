# Web assets - includes new ws.js and api.js

idf_component_register(
    SRCS 
        "main.c"
        "app/app.c"
        "finger/finger.c"
        "finger/finger_meta.c"
        "button/button.c"
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
        "wifi"
        "webserver"
        "settings"
        "ota"
    REQUIRES 
        khoa_as608 
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
        khoa_audio
    EMBED_FILES ${WEB_ASSETS}
)