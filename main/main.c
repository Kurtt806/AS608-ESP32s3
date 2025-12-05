/**
 * @file main.c
 * @brief Application Entry Point
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"

#include "settings/settings.h"
#include "app/app.h"
#include "webserver/webserver.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "=========================================");
    ESP_LOGI(TAG, "  AS608 Fingerprint System");
    ESP_LOGI(TAG, "  ESP32-S3 | ESP-IDF v5.x");
    ESP_LOGI(TAG, "=========================================");

    /* Create default event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Initialize settings (must be after NVS) */
    ret = settings_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Settings init failed: %s", esp_err_to_name(ret));
    } else {
        settings_dump();  /* Debug: print current settings */
    }

    /* Initialize web server */
    webserver_init();

    /* Initialize and start application */
    ret = app_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "App init failed: %s", esp_err_to_name(ret));
        return;
    }



    app_start();
    
    /* Start web server after app is running */
    webserver_start();


}
