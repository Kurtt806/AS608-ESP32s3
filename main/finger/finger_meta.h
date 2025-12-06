/**
 * @file finger_meta.h
 * @brief Fingerprint Metadata Management - Store names for each fingerprint ID
 * 
 * This module provides persistent storage for fingerprint metadata (names).
 * Data is stored in NVS and survives power cycles.
 * 
 * Design:
 * - Each fingerprint ID can have a name (max 32 chars)
 * - Names are stored in NVS as key-value pairs (key: "fn_XXX", value: name)
 * - Thread-safe with mutex protection
 * 
 * @example
 *   finger_meta_init();
 *   finger_meta_set_name(5, "Nguyen Van A");
 *   const char* name = finger_meta_get_name(5);  // Returns "Nguyen Van A"
 */

#ifndef FINGER_META_H
#define FINGER_META_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration
 * ============================================================================ */

/** @brief Maximum length of fingerprint name (including null terminator) */
#define FINGER_NAME_MAX_LEN         32

/** @brief NVS namespace for fingerprint metadata */
#define FINGER_META_NVS_NAMESPACE   "finger_meta"

/** @brief Maximum number of fingerprints (matches AS608 library size) */
#define FINGER_META_MAX_COUNT       162

/* ============================================================================
 * Data Types
 * ============================================================================ */

/**
 * @brief Fingerprint metadata entry
 */
typedef struct {
    int16_t     id;                         /**< Fingerprint ID (0-161) */
    char        name[FINGER_NAME_MAX_LEN];  /**< User-defined name */
    uint32_t    created_at;                 /**< Creation timestamp (seconds since boot or epoch) */
    uint32_t    last_match;                 /**< Last match timestamp */
    uint16_t    match_count;                /**< Total successful matches */
} finger_meta_entry_t;

/* ============================================================================
 * Core API
 * ============================================================================ */

/**
 * @brief Initialize metadata module
 * @note Must be called after nvs_flash_init()
 * @return ESP_OK on success
 */
esp_err_t finger_meta_init(void);

/**
 * @brief Deinitialize metadata module
 */
void finger_meta_deinit(void);

/* ============================================================================
 * Name Management
 * ============================================================================ */

/**
 * @brief Set name for a fingerprint ID
 * @param id Fingerprint ID (0 to FINGER_META_MAX_COUNT-1)
 * @param name Name string (max FINGER_NAME_MAX_LEN-1 chars, null-terminated)
 * @return ESP_OK on success
 */
esp_err_t finger_meta_set_name(int id, const char *name);

/**
 * @brief Get name for a fingerprint ID
 * @param id Fingerprint ID
 * @return Name string or NULL if not set. Do not modify or free returned string.
 */
const char* finger_meta_get_name(int id);

/**
 * @brief Check if a fingerprint ID has a name
 * @param id Fingerprint ID
 * @return true if name exists
 */
bool finger_meta_has_name(int id);

/**
 * @brief Delete name for a fingerprint ID
 * @param id Fingerprint ID
 * @return ESP_OK on success
 */
esp_err_t finger_meta_delete_name(int id);

/**
 * @brief Clear all names
 * @return ESP_OK on success
 */
esp_err_t finger_meta_clear_all(void);

/* ============================================================================
 * Full Metadata Management
 * ============================================================================ */

/**
 * @brief Get full metadata entry for a fingerprint ID
 * @param id Fingerprint ID
 * @param[out] entry Pointer to store metadata
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no metadata
 */
esp_err_t finger_meta_get_entry(int id, finger_meta_entry_t *entry);

/**
 * @brief Set full metadata entry for a fingerprint ID
 * @param id Fingerprint ID
 * @param entry Pointer to metadata entry
 * @return ESP_OK on success
 */
esp_err_t finger_meta_set_entry(int id, const finger_meta_entry_t *entry);

/**
 * @brief Update match statistics for a fingerprint ID
 * Called automatically when fingerprint is matched.
 * @param id Fingerprint ID
 * @return ESP_OK on success
 */
esp_err_t finger_meta_record_match(int id);

/**
 * @brief Create new metadata entry with auto-generated name
 * @param id Fingerprint ID
 * @param name Optional custom name (NULL for auto-generated "ID_XXX")
 * @return ESP_OK on success
 */
esp_err_t finger_meta_create(int id, const char *name);

/* ============================================================================
 * Query API
 * ============================================================================ */

/**
 * @brief Get count of fingerprints with metadata
 * @return Number of entries
 */
int finger_meta_count(void);

/**
 * @brief Iterate all metadata entries
 * @param callback Callback function for each entry, return false to stop iteration
 * @param user_data User data passed to callback
 */
typedef bool (*finger_meta_iterate_cb)(const finger_meta_entry_t *entry, void *user_data);
void finger_meta_iterate(finger_meta_iterate_cb callback, void *user_data);

#ifdef __cplusplus
}
#endif

#endif /* FINGER_META_H */
