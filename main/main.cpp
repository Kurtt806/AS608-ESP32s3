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
#include "as608_manager.h"
#include <wifi_manager.h>

static const char *TAG = "MAIN";

void fingerprint_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Fingerprint detection task started");

    auto &as608 = AS608Manager::GetInstance();

    static bool first_loop = true;
    bool enrolling = false;
    int enroll_id = 0;

    while (1)
    {
        if (first_loop) {
            ESP_LOGI(TAG, "Fingerprint task loop started");
            first_loop = false;
        }

        // Check for finger
        esp_err_t ret = as608.ReadImage();
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "Finger detected, processing...");

            // Generate character file
            ret = as608.GenerateCharacter(1);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to generate character file: %s", esp_err_to_name(ret));
                enrolling = false;
                vTaskDelay(pdMS_TO_TICKS(2000));
                continue;
            }

            if (!enrolling)
            {
                // Search in library
                int match_id = -1;
                uint16_t score = 0;
                ret = as608.SearchTemplate(&match_id, &score);
                if (ret == ESP_OK)
                {
                    ESP_LOGI(TAG, "Fingerprint matched! ID: %d, Score: %d", match_id, score);
                    // TODO: Handle successful match (e.g., unlock door, log event)
                }
                else if (ret == ESP_ERR_NOT_FOUND)
                {
                    ESP_LOGW(TAG, "Fingerprint not found in library, starting auto-enrollment");

                    // Get next available ID
                    uint16_t count = 0;
                    as608.GetTemplateCount(&count);
                    enroll_id = count + 1;

                    enrolling = true;
                    ESP_LOGI(TAG, "Auto-enrollment started for ID %d. Place finger again.", enroll_id);

                    // Wait for second placement
                    vTaskDelay(pdMS_TO_TICKS(3000));
                    continue; // Skip delay, check again immediately
                }
                else
                {
                    ESP_LOGE(TAG, "Search failed: %s", esp_err_to_name(ret));
                }
            }
            else
            {
                // Second scan for enrollment
                ESP_LOGI(TAG, "Second scan for enrollment ID %d", enroll_id);

                ret = as608.GenerateCharacter(2);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to generate character file 2: %s", esp_err_to_name(ret));
                    enrolling = false;
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    continue;
                }

                ret = as608.RegisterModel();
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to register model: %s", esp_err_to_name(ret));
                    enrolling = false;
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    continue;
                }

                ret = as608.StoreTemplate(enroll_id);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to store template: %s", esp_err_to_name(ret));
                    enrolling = false;
                    vTaskDelay(pdMS_TO_TICKS(2000));
                    continue;
                }

                ESP_LOGI(TAG, "Auto-enrollment completed successfully for ID %d", enroll_id);
                enrolling = false;
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
            enrolling = false;
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
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

    /* Initialize WiFi Manager */
    auto &wifi = WifiManager::GetInstance();
    WifiManagerConfig wifi_config;
    wifi_config.ssid_prefix = "AS608-FP";
    wifi_config.language = "vi";
    if (!wifi.Initialize(wifi_config))
    {
        ESP_LOGE(TAG, "WiFi Manager init failed");
    }

    /* Set WiFi event callback to auto-connect after config */
    wifi.SetEventCallback([](WifiEvent event)
                          {
        if (event == WifiEvent::ConfigModeExit) {
            ESP_LOGI(TAG, "Config mode exited, starting station mode");
            auto &wifi = WifiManager::GetInstance();
            wifi.StartStation();
        } });

    /* Initialize button */
    ret = ButtonManager::GetInstance().Initialize();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "ButtonManager init failed: %s", esp_err_to_name(ret));
    }
    else
    {
        ESP_LOGI(TAG, "ButtonManager initialized successfully");
    }

    /* Initialize AS608 fingerprint sensor */
    auto &as608 = AS608Manager::GetInstance();
    AS608Config as608_cfg = {
        .uart_num = CFG_AS608_UART_PORT,
        .tx_pin = CFG_AS608_TX_GPIO,
        .rx_pin = CFG_AS608_RX_GPIO,
        .baudrate = CFG_AS608_BAUD_RATE};
    ret = as608.Initialize(as608_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "AS608 init failed: %s", esp_err_to_name(ret));
    }

    /* Start fingerprint detection task */
    BaseType_t task_ret = xTaskCreate(fingerprint_task, "fingerprint_task", 4096, NULL, 5, NULL);
    if (task_ret == pdPASS)
    {
        ESP_LOGI(TAG, "Fingerprint task created successfully");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to create fingerprint task");
    }

    /* Set button event callback */
    ButtonManager::GetInstance().SetEventCallback([](ButtonEvent event, button_id_t id)
                                                  {
        ESP_LOGI(TAG, "Button event: %d for button %d", static_cast<int>(event), id);
        if (event == ButtonEvent::LongPress && id == BTN_ID_BOOT) {
            ESP_LOGI(TAG, "Entering WiFi configuration mode...");
            auto &wifi = WifiManager::GetInstance();
            wifi.StartConfigAp();
        } });
}
