/**
 * @file wifi.h
 * @brief WiFi Module API - C wrapper for khoa_esp_wifi
 */

#ifndef WIFI_H
#define WIFI_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize WiFi module
 * @return ESP_OK on success
 */
esp_err_t wifi_module_init(void);

/**
 * @brief Start WiFi station mode
 * Connects to saved networks
 */
void wifi_module_start(void);

/**
 * @brief Stop WiFi
 */
void wifi_module_stop(void);

/**
 * @brief Check if WiFi is connected
 * @return true if connected
 */
bool wifi_module_is_connected(void);

/**
 * @brief Wait for WiFi connection
 * @param timeout_ms Timeout in milliseconds
 * @return true if connected within timeout
 */
bool wifi_module_wait_connected(int timeout_ms);

/**
 * @brief Start WiFi configuration AP mode
 * Starts a captive portal for WiFi configuration
 */
void wifi_module_start_config_ap(void);

/**
 * @brief Stop WiFi configuration AP
 */
void wifi_module_stop_config_ap(void);

/**
 * @brief Get current RSSI
 * @return RSSI value in dBm
 */
int8_t wifi_module_get_rssi(void);

/**
 * @brief Enable/disable power save mode
 * @param enabled true to enable
 */
void wifi_module_set_power_save(bool enabled);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_H */
