/**
 * @file wifi.cc
 * @brief WiFi Module Implementation - C++ wrapper using khoa_esp_wifi
 */

#include "wifi.h"
#include "../common/config.h"
#include "wifi_station.h"
#include "wifi_configuration_ap.h"
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

    /* Load saved SSIDs from NVS and add to WifiStation */
    auto& ssid_manager = SsidManager::GetInstance();
    auto& wifi_station = WifiStation::GetInstance();

    const auto& ssid_list = ssid_manager.GetSsidList();
    ESP_LOGI(TAG, "Found %d saved SSID(s) in NVS", ssid_list.size());
    
    if (ssid_list.empty()) {
        ESP_LOGW(TAG, "No saved WiFi networks. Use WiFi config mode (double-click BOOT) to add.");
    }
    
    for (const auto& item : ssid_list) {
        ESP_LOGI(TAG, "Adding saved SSID: %s", item.ssid.c_str());
        wifi_station.AddAuth(std::string(item.ssid), std::string(item.password));
    }

    /* Set up callbacks */
    wifi_station.OnConnect([](const std::string& ssid) {
        ESP_LOGI(TAG, "Connecting to: %s", ssid.c_str());
    });

    wifi_station.OnConnected([](const std::string& ssid) {
        ESP_LOGI(TAG, "Connected to: %s", ssid.c_str());
    });

    wifi_station.OnScanBegin([]() {
        ESP_LOGI(TAG, "Scanning for networks...");
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
    WifiStation::GetInstance().Start();
}

void wifi_module_stop(void)
{
    ESP_LOGI(TAG, "Stopping WiFi...");
    WifiStation::GetInstance().Stop();
}

bool wifi_module_is_connected(void)
{
    return WifiStation::GetInstance().IsConnected();
}

bool wifi_module_wait_connected(int timeout_ms)
{
    return WifiStation::GetInstance().WaitForConnected(timeout_ms);
}

void wifi_module_start_config_ap(void)
{
    ESP_LOGI(TAG, "Starting WiFi configuration AP...");
    
    auto& config_ap = WifiConfigurationAp::GetInstance();
    config_ap.SetSsidPrefix(CFG_WIFI_AP_PREFIX);
    config_ap.SetLanguage(CFG_WIFI_LANGUAGE);
    config_ap.Start();
    
    ESP_LOGI(TAG, "Configuration AP started. URL: %s", config_ap.GetWebServerUrl().c_str());
}

void wifi_module_stop_config_ap(void)
{
    ESP_LOGI(TAG, "Stopping WiFi configuration AP...");
    WifiConfigurationAp::GetInstance().Stop();
}

int8_t wifi_module_get_rssi(void)
{
    return WifiStation::GetInstance().GetRssi();
}

void wifi_module_set_power_save(bool enabled)
{
    WifiStation::GetInstance().SetPowerSaveMode(enabled);
}

} // extern "C"
