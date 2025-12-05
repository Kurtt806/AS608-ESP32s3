/**
 * @file app.h
 * @brief Application Controller API
 */

#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include "esp_err.h"
#include "../common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize application
 * @return ESP_OK on success
 */
esp_err_t app_init(void);

/**
 * @brief Start application (after init)
 */
void app_start(void);

/**
 * @brief Stop application
 */
void app_stop(void);

/**
 * @brief Get current state
 */
app_state_t app_get_state(void);

/**
 * @brief Get state as string
 */
const char* app_get_state_string(void);

/**
 * @brief Start enrollment
 * @return Assigned finger ID, or -1 on error
 */
int16_t app_start_enroll(void);

/**
 * @brief Cancel current operation
 */
void app_cancel(void);

/**
 * @brief Delete fingerprint
 * @param finger_id Slot to delete (-1 for all)
 */
esp_err_t app_delete_finger(int16_t finger_id);

/**
 * @brief Start WiFi configuration mode
 */
void app_start_wifi_config(void);

/**
 * @brief Stop WiFi configuration mode
 */
void app_stop_wifi_config(void);

/* ============================================================================
 * WebSocket Request Functions (thread-safe)
 * ============================================================================ */

/**
 * @brief Request enrollment from web interface
 */
void app_request_enroll(void);

/**
 * @brief Request search from web interface
 */
void app_request_search(void);

/**
 * @brief Request cancel from web interface
 */
void app_request_cancel(void);

/**
 * @brief Request delete specific fingerprint
 */
void app_request_delete(int id);

/**
 * @brief Request delete all fingerprints
 */
void app_request_delete_all(void);

/**
 * @brief Set auto search mode
 */
void app_set_auto_search(bool enabled);

/**
 * @brief Get auto search mode
 */
bool app_get_auto_search(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_H */
