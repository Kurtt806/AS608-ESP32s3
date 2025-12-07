/**
 * @file main.c
 * @brief Application Entry Point
 * @author KH OA
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"


static const char *TAG = "MAIN";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=========================================");
    ESP_LOGI(TAG, "  AS608 Fingerprint System");
    ESP_LOGI(TAG, "  ESP32-S3 | ESP-IDF v5.x");
    ESP_LOGI(TAG, "=========================================");

    // Bỏ ghi chú dòng bên dưới để tắt tất cả nhật ký ESP
    //  esp_log_level_set("*", ESP_LOG_NONE);
    //  esp_log_level_set("AUDIO", ESP_LOG_NONE);

    /*Create default event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "Erasing NVS flash");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}
