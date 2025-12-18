/**
 * @file main.c
 * @brief Application Entry Point
 * @author KH OA
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "driver/uart.h"

#include "settings/settings.h"
#include "common/config.h"
#include "button/button.h"
#include "button/button_events.h"
extern "C" {
#include "as608.h"
}
#include <wifi_manager.h>

static const char *TAG = "MAIN";

static void button_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    btn_event_data_t* data = (btn_event_data_t*) event_data;
    ESP_LOGI(TAG, "Button event: %d for button %d", (int)event_id, data->btn_id);

    if (event_id == BUTTON_EVT_LONG_PRESS && data->btn_id == BTN_ID_BOOT)
    {
        ESP_LOGI(TAG, "Entering WiFi configuration mode...");
        auto& wifi = WifiManager::GetInstance();
        wifi.StartConfigAp();
    }
}

void fingerprint_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Fingerprint detection task started");

    while (1)
    {
        // Check for finger
        esp_err_t ret = as608_read_image();
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "Finger detected, processing...");

            // Generate character file
            ret = as608_gen_char(1);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to generate character file: %s", esp_err_to_name(ret));
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }

            // Search in library
            int match_id = -1;
            uint16_t score = 0;
            ret = as608_search(&match_id, &score);
            if (ret == ESP_OK)
            {
                ESP_LOGI(TAG, "Fingerprint matched! ID: %d, Score: %d", match_id, score);
                // TODO: Handle successful match (e.g., unlock door, log event)
            }
            else if (ret == ESP_ERR_NOT_FOUND)
            {
                ESP_LOGW(TAG, "Fingerprint not found in library");
            }
            else
            {
                ESP_LOGE(TAG, "Search failed: %s", esp_err_to_name(ret));
            }

            // Delay after processing to avoid multiple detections
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            // No finger, continue checking
            vTaskDelay(pdMS_TO_TICKS(500)); // Check every 500ms
        }
        else
        {
            ESP_LOGE(TAG, "Read image failed: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

void fingerprint_read(void)
{
    // This function is now unused, but kept for compatibility
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=========================================");
    ESP_LOGI(TAG, "  AS608 Fingerprint System");
    ESP_LOGI(TAG, "  ESP32-S3 | ESP-IDF v5.x");
    ESP_LOGI(TAG, "=========================================");

    // Bỏ ghi chú dòng bên dưới để tắt tất cả nhật ký ESP
    //  esp_log_level_set("*", ESP_LOG_NONE);

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

    /* Initialize settings */
    ret = settings_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Settings init failed: %s", esp_err_to_name(ret));
    }
    ESP_ERROR_CHECK(ret);

    /* Initialize button */
    ret = button_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Button init failed: %s", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "Button initialized successfully");
    }

    /* Register button event handler */
    ret = esp_event_handler_register(BUTTON_EVENT, ESP_EVENT_ANY_ID, button_event_handler, NULL);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register button event handler: %s", esp_err_to_name(ret));
    }

    /* Initialize AS608 fingerprint sensor */
    as608_config_t as608_cfg = {
        .uart_num = CFG_AS608_UART_PORT,
        .tx_pin = CFG_AS608_TX_GPIO,
        .rx_pin = CFG_AS608_RX_GPIO,
        .baudrate = CFG_AS608_BAUD_RATE
    };
    ret = as608_init(&as608_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "AS608 init failed: %s", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "AS608 initialized successfully");
    }

    /* Initialize WiFi Manager */
    auto& wifi = WifiManager::GetInstance();
    WifiManagerConfig wifi_config;
    wifi_config.ssid_prefix = "AS608-FP";
    wifi_config.language = "vi";
    if (!wifi.Initialize(wifi_config))
    {
        ESP_LOGE(TAG, "WiFi Manager init failed");
    }
    else
    {
        ESP_LOGI(TAG, "WiFi Manager initialized successfully");
    }

    /* Start fingerprint detection task */
    xTaskCreate(fingerprint_task, "fingerprint_task", 4096, NULL, 5, NULL);



}
