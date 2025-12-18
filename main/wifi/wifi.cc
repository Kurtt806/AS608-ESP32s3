/**
 * @file wifi.cc
 * @brief WiFi Module Implementation - C++ wrapper using khoa_esp_wifi
 */

#include "wifi.h"
#include "../common/config.h"
#include "wifi_manager.h"
#include "ssid_manager.h"

#include "esp_log.h"
#include "esp_netif.h"

static const char *TAG = "WIFI";

/* ============================================================================
 * C API Implementation
 * ============================================================================ */

extern "C" {

esp_err_t wifi_module_init(void)
{
    ESP_LOGI(TAG, "Initializing WiFi module...");

    /* Initialize TCP/IP stack */
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Initialize WifiManager */
    auto& wifi_manager = WifiManager::GetInstance();
    WifiManagerConfig config;
    wifi_manager.Initialize(config);

    /* Load saved SSIDs from NVS */
    auto& ssid_manager = SsidManager::GetInstance();
    const auto& ssid_list = ssid_manager.GetSsidList();
    ESP_LOGI(TAG, "Found %d saved SSID(s) in NVS", ssid_list.size());
    
    if (ssid_list.empty()) {
        ESP_LOGW(TAG, "No saved WiFi networks. Use WiFi config mode (double-click BOOT) to add.");
    }
    
    // Note: WifiManager handles SSID management internally

    /* Set up callbacks */
    wifi_manager.SetEventCallback([&wifi_manager](WifiEvent event) {
        switch (event) {
            case WifiEvent::Connecting:
                ESP_LOGI(TAG, "Connecting to WiFi...");
                break;
            case WifiEvent::Connected:
                ESP_LOGI(TAG, "Connected to WiFi: %s", wifi_manager.GetSsid().c_str());
                break;
            case WifiEvent::Disconnected:
                ESP_LOGI(TAG, "Disconnected from WiFi");
                break;
            case WifiEvent::ConfigModeEnter:
                ESP_LOGI(TAG, "Entered configuration mode");
                break;
            case WifiEvent::ConfigModeExit:
                ESP_LOGI(TAG, "Exited configuration mode");
                break;
            default:
                break;
        }
    });

    ESP_LOGI(TAG, "WiFi module initialized");
    return ESP_OK;
}

void wifi_module_start(void)
{
    auto& ssid_manager = SsidManager::GetInstance();
    
    /* If no saved networks, start config AP mode automatically */
    if (ssid_manager.GetSsidList().empty()) {
        ESP_LOGW(TAG, "No saved WiFi networks - starting config AP mode");
        wifi_module_start_config_ap();
        return;
    }
    
    ESP_LOGI(TAG, "Starting WiFi station...");
    auto& wifi_manager = WifiManager::GetInstance();
    wifi_manager.StartStation();
}

void wifi_module_stop(void)
{
    ESP_LOGI(TAG, "Stopping WiFi...");
    auto& wifi_manager = WifiManager::GetInstance();
    wifi_manager.StopStation();
    wifi_manager.StopConfigAp();
}

bool wifi_module_is_connected(void)
{
    auto& wifi_manager = WifiManager::GetInstance();
    return wifi_manager.IsConnected();
}

bool wifi_module_wait_connected(int timeout_ms)
{
    auto& wifi_manager = WifiManager::GetInstance();
    // Note: The new API might not have WaitForConnected, so we'll implement a simple wait
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (wifi_manager.IsConnected()) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
        elapsed += 100;
    }
    return false;
}

void wifi_module_start_config_ap(void)
{
    ESP_LOGI(TAG, "Starting WiFi configuration AP...");
    
    auto& wifi_manager = WifiManager::GetInstance();
    wifi_manager.StartConfigAp();
    
    ESP_LOGI(TAG, "Configuration AP started. URL: %s", wifi_manager.GetApWebUrl().c_str());
}

void wifi_module_stop_config_ap(void)
{
    ESP_LOGI(TAG, "Stopping WiFi configuration AP...");
    auto& wifi_manager = WifiManager::GetInstance();
    wifi_manager.StopConfigAp();
}

int8_t wifi_module_get_rssi(void)
{
    auto& wifi_manager = WifiManager::GetInstance();
    return wifi_manager.GetRssi();
}

void wifi_module_set_power_save(bool enabled)
{
    auto& wifi_manager = WifiManager::GetInstance();
    WifiPowerSaveLevel level = enabled ? WifiPowerSaveLevel::LOW_POWER : WifiPowerSaveLevel::PERFORMANCE;
    wifi_manager.SetPowerSaveLevel(level);
}

} // extern "C"
