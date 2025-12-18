/**
 * @file main.cpp
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
#include "as608_manager.h"
#include <wifi_manager.h>
#include "iot_button.h"

static const char *TAG = "MAIN";

enum class FingerprintState
{
    IDLE,
    ENROLLING_FIRST,
    ENROLLING_SECOND
};

void handle_finger_detected(auto &as608, FingerprintState &state, int &enroll_id)
{
    ESP_LOGI(TAG, "Finger detected, processing...");

    esp_err_t ret = as608.GenerateCharacter(1);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Generate character failed: %s", esp_err_to_name(ret));
        state = FingerprintState::IDLE;
        return;
    }

    if (state == FingerprintState::IDLE)
    {
        int match_id = -1;
        uint16_t score = 0;
        ret = as608.SearchTemplate(&match_id, &score);
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "Match found! ID: %d, Score: %d", match_id, score);
            // TODO: Handle match
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGW(TAG, "Not found, starting enrollment");
            uint16_t count = 0;
            as608.GetTemplateCount(&count);
            enroll_id = count + 1;
            state = FingerprintState::ENROLLING_FIRST;
            ESP_LOGI(TAG, "Place finger again for ID %d", enroll_id);
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
        else
        {
            ESP_LOGE(TAG, "Search failed: %s", esp_err_to_name(ret));
        }
    }
    else if (state == FingerprintState::ENROLLING_FIRST)
    {
        ESP_LOGI(TAG, "Second scan for ID %d", enroll_id);
        ret = as608.GenerateCharacter(2);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Generate character 2 failed: %s", esp_err_to_name(ret));
            state = FingerprintState::IDLE;
            return;
        }

        ret = as608.RegisterModel();
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Register model failed: %s", esp_err_to_name(ret));
            state = FingerprintState::IDLE;
            return;
        }

        ret = as608.StoreTemplate(enroll_id);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Store template failed: %s", esp_err_to_name(ret));
            state = FingerprintState::IDLE;
            return;
        }

        ESP_LOGI(TAG, "Enrollment completed for ID %d", enroll_id);
        state = FingerprintState::IDLE;
    }
}

void fingerprint_task(void *pvParameters)
{
    auto &as608 = AS608Manager::GetInstance();
    FingerprintState state = FingerprintState::IDLE;
    int enroll_id = 0;

    while (true)
    {
        esp_err_t ret = as608.ReadImage();
        if (ret == ESP_OK)
        {
            handle_finger_detected(as608, state, enroll_id);
        }
        else if (ret == ESP_ERR_NOT_FOUND)
        {
            vTaskDelay(pdMS_TO_TICKS(500)); // Check every 500ms
        }
        else
        {
            ESP_LOGE(TAG, "Read image failed: %s", esp_err_to_name(ret));
            state = FingerprintState::IDLE;
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
    // esp_log_level_set("*", ESP_LOG_NONE);
    // esp_log_level_set("*", ESP_LOG_NONE);

    /*Create default event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Initialize NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* Initialize settings */
    ESP_ERROR_CHECK(settings_init());

    /* Initialize WiFi Manager */
    auto &wifi = WifiManager::GetInstance();
    WifiManagerConfig wifi_config{.ssid_prefix = "AS608-FP", .language = "vi"};
    if (!wifi.Initialize(wifi_config))
    {
        ESP_LOGE(TAG, "WiFi Manager init failed");
    }
    else
    {
        ESP_LOGI(TAG, "WiFi Manager initialized successfully");
        // kết nối tự động khi khởi động
        wifi.StartStation();
    }
    /* Set WiFi event callback to auto-connect after config */
    wifi.SetEventCallback([](WifiEvent event)
                          {
        if (event == WifiEvent::ConfigModeExit) {
            auto &wifi = WifiManager::GetInstance();
            wifi.StartStation();
        } });

    /* Initialize button */
    button_config_t button_config = {};
    button_config.type = BUTTON_TYPE_GPIO;
    button_config.gpio_button_config.gpio_num = CFG_BTN_BOOT_GPIO;
    button_config.gpio_button_config.active_level = CFG_BTN_ACTIVE; // active low
    button_handle_t button_handle = iot_button_create(&button_config);
    if (button_handle == NULL)
    {
        ESP_LOGE(TAG, "Failed to create button");
    }
    else
    {
        ESP_LOGI(TAG, "Button initialized successfully");
    }

    /* Set button event callbacks */
    iot_button_register_cb(button_handle, BUTTON_SINGLE_CLICK, [](void *usr_data, void *event_data)
                           { ESP_LOGI(TAG, "Button single click"); }, NULL);

    iot_button_register_cb(button_handle, BUTTON_DOUBLE_CLICK, [](void *usr_data, void *event_data)
                           { ESP_LOGI(TAG, "Button double click"); }, NULL);

    iot_button_register_cb(button_handle, BUTTON_LONG_PRESS_START, [](void *usr_data, void *event_data)
                           {
        ESP_LOGI(TAG, "Button long press, entering WiFi config mode...");
        auto &wifi = WifiManager::GetInstance();
        wifi.StartConfigAp(); }, NULL);

    /* Initialize AS608 fingerprint sensor */
    auto &as608 = AS608Manager::GetInstance();
    AS608Config as608_cfg = {
        .uart_num = CFG_AS608_UART_PORT,
        .tx_pin = CFG_AS608_TX_GPIO,
        .rx_pin = CFG_AS608_RX_GPIO,
        .baudrate = CFG_AS608_BAUD_RATE};
    if (as608.Initialize(as608_cfg) != ESP_OK)
    {
        ESP_LOGE(TAG, "AS608 init failed");
    }
    /* Start fingerprint detection task */
    xTaskCreate(fingerprint_task, "fingerprint_task", 4096, NULL, 5, NULL);
}
