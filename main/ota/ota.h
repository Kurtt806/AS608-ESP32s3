/**
 * @file ota.h
 * @brief OTA (Over-The-Air) Update Module
 * 
 * Provides firmware update functionality via:
 * - HTTP upload from web interface
 * - URL-based download from remote server
 * 
 * Features:
 * - Progress tracking with callbacks
 * - Rollback support
 * - Firmware validation
 * - Version management
 */

#ifndef OTA_H
#define OTA_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration
 * ============================================================================ */

/** @brief OTA receive buffer size */
#define OTA_BUFFER_SIZE         4096

/** @brief OTA timeout in seconds */
#define OTA_TIMEOUT_SEC         120

/** @brief Maximum firmware URL length */
#define OTA_URL_MAX_LEN         256

/* ============================================================================
 * Data Types
 * ============================================================================ */

/**
 * @brief OTA update state
 */
typedef enum {
    OTA_STATE_IDLE = 0,         /**< No update in progress */
    OTA_STATE_STARTING,         /**< Preparing for update */
    OTA_STATE_DOWNLOADING,      /**< Downloading/receiving firmware */
    OTA_STATE_VERIFYING,        /**< Verifying firmware */
    OTA_STATE_APPLYING,         /**< Applying update */
    OTA_STATE_COMPLETE,         /**< Update complete, pending reboot */
    OTA_STATE_ERROR,            /**< Update failed */
    OTA_STATE_ROLLING_BACK,     /**< Rolling back to previous version */
} ota_state_t;

/**
 * @brief OTA progress information
 */
typedef struct {
    ota_state_t state;          /**< Current state */
    uint32_t    total_size;     /**< Total firmware size (0 if unknown) */
    uint32_t    received_size;  /**< Bytes received so far */
    uint8_t     progress;       /**< Progress percentage (0-100) */
    const char  *message;       /**< Status message */
    int         error_code;     /**< Error code (if state == OTA_STATE_ERROR) */
} ota_progress_t;

/**
 * @brief OTA progress callback function
 */
typedef void (*ota_progress_cb_t)(const ota_progress_t *progress, void *user_data);

/**
 * @brief Firmware information
 */
typedef struct {
    char        version[32];        /**< Firmware version string */
    char        project_name[32];   /**< Project name */
    char        compile_date[24];   /**< Compile date */
    char        compile_time[16];   /**< Compile time */
    char        idf_version[16];    /**< ESP-IDF version */
    uint32_t    app_size;           /**< Application size */
    bool        is_factory;         /**< Running from factory partition */
    bool        can_rollback;       /**< Rollback available */
} ota_firmware_info_t;

/* ============================================================================
 * Core API
 * ============================================================================ */

/**
 * @brief Initialize OTA module
 * @return ESP_OK on success
 */
esp_err_t ota_init(void);

/**
 * @brief Deinitialize OTA module
 */
void ota_deinit(void);

/**
 * @brief Get current OTA state
 * @return Current state
 */
ota_state_t ota_get_state(void);

/**
 * @brief Get current progress information
 * @param[out] progress Pointer to store progress info
 * @return ESP_OK on success
 */
esp_err_t ota_get_progress(ota_progress_t *progress);

/**
 * @brief Set progress callback
 * @param callback Callback function
 * @param user_data User data passed to callback
 */
void ota_set_progress_callback(ota_progress_cb_t callback, void *user_data);

/* ============================================================================
 * Firmware Info API
 * ============================================================================ */

/**
 * @brief Get current firmware information
 * @param[out] info Pointer to store firmware info
 * @return ESP_OK on success
 */
esp_err_t ota_get_firmware_info(ota_firmware_info_t *info);

/**
 * @brief Get running partition label
 * @return Partition label string
 */
const char* ota_get_running_partition(void);

/* ============================================================================
 * Update API
 * ============================================================================ */

/**
 * @brief Start OTA update session for chunked upload
 * @param total_size Total firmware size (0 if unknown)
 * @return ESP_OK on success
 */
esp_err_t ota_begin(uint32_t total_size);

/**
 * @brief Write firmware data chunk
 * @param data Pointer to data
 * @param len Data length
 * @return ESP_OK on success
 */
esp_err_t ota_write(const void *data, size_t len);

/**
 * @brief Finish OTA update and verify
 * @return ESP_OK on success
 */
esp_err_t ota_end(void);

/**
 * @brief Abort current OTA update
 * @return ESP_OK on success
 */
esp_err_t ota_abort(void);

/**
 * @brief Start OTA update from URL
 * @param url Firmware download URL
 * @return ESP_OK on success (download starts in background)
 */
esp_err_t ota_start_url(const char *url);

/* ============================================================================
 * Post-Update API
 * ============================================================================ */

/**
 * @brief Mark current firmware as valid
 * Call this after successful boot to prevent rollback
 * @return ESP_OK on success
 */
esp_err_t ota_mark_valid(void);

/**
 * @brief Rollback to previous firmware
 * @return ESP_OK on success (device will reboot)
 */
esp_err_t ota_rollback(void);

/**
 * @brief Reboot device to apply update
 */
void ota_reboot(void);

/**
 * @brief Check if pending OTA update needs validation
 * @return true if pending validation
 */
bool ota_is_pending_verify(void);

#ifdef __cplusplus
}
#endif

#endif /* OTA_H */
