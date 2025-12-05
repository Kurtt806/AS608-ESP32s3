/**
 * @file finger.h
 * @brief Fingerprint Module API
 */

#ifndef FINGER_H
#define FINGER_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Commands
 * ============================================================================ */
typedef enum {
    FINGER_CMD_IDLE = 0,
    FINGER_CMD_SEARCH,
    FINGER_CMD_ENROLL,
    FINGER_CMD_CANCEL,
} finger_cmd_t;

/* ============================================================================
 * API
 * ============================================================================ */

/**
 * @brief Initialize fingerprint module (sensor + task)
 * @return ESP_OK on success
 */
esp_err_t finger_init(void);

/**
 * @brief Deinitialize fingerprint module
 */
void finger_deinit(void);

/**
 * @brief Start search mode
 */
esp_err_t finger_start_search(void);

/**
 * @brief Start enrollment
 * @param finger_id Target slot ID
 */
esp_err_t finger_start_enroll(int16_t finger_id);

/**
 * @brief Cancel current operation
 */
esp_err_t finger_cancel(void);

/**
 * @brief Get stored template count
 * @param count Output
 */
esp_err_t finger_get_count(uint16_t *count);

/**
 * @brief Find next available slot
 * @return Slot ID or -1 if full
 */
int16_t finger_find_free_slot(void);

/**
 * @brief Delete single fingerprint
 * @param finger_id Slot to delete
 */
esp_err_t finger_delete(int16_t finger_id);

/**
 * @brief Delete all fingerprints
 */
esp_err_t finger_delete_all(void);

/**
 * @brief Check if sensor is connected
 */
bool finger_is_connected(void);

/**
 * @brief Get total library size (capacity)
 */
uint16_t finger_get_library_size(void);

/**
 * @brief Get current template count
 */
uint16_t finger_get_template_count(void);

/**
 * @brief Check if fingerprint ID is used
 */
bool finger_is_id_used(int id);

/**
 * @brief Search once (single search, not continuous)
 */
esp_err_t finger_search_once(void);

#ifdef __cplusplus
}
#endif

#endif /* FINGER_H */
