/**
 * @file settings.h
 * @brief Settings Module API - Persistent configuration storage using NVS
 * 
 * This module provides a clean, type-safe API for storing and retrieving
 * application settings. All settings are automatically persisted to NVS.
 * 
 * @note Thread-safe with mutex protection
 * 
 * @example
 *   settings_init();
 *   settings_set_volume(80);
 *   uint8_t vol = settings_get_volume();
 *   settings_save();  // Optional - auto-saved on set if SETTINGS_AUTO_SAVE is enabled
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Configuration
 * ============================================================================ */

/** @brief Auto-save on every set operation (default: true) */
#ifndef SETTINGS_AUTO_SAVE
#define SETTINGS_AUTO_SAVE          1
#endif

/** @brief NVS namespace for settings */
#ifndef SETTINGS_NVS_NAMESPACE
#define SETTINGS_NVS_NAMESPACE      "settings"
#endif

/* ============================================================================
 * Default Values
 * ============================================================================ */

#define SETTINGS_DEFAULT_VOLUME         50
#define SETTINGS_DEFAULT_BRIGHTNESS     80
#define SETTINGS_DEFAULT_LANGUAGE       "vi"
#define SETTINGS_DEFAULT_DEVICE_MODE    0
#define SETTINGS_DEFAULT_SOUND_ENABLED  true
#define SETTINGS_DEFAULT_POWER_SAVE     false
#define SETTINGS_DEFAULT_AUTO_LOCK_SEC  300
#define SETTINGS_DEFAULT_LCD_TIMEOUT    30

/* ============================================================================
 * Data Types
 * ============================================================================ */

/**
 * @brief Device operating mode
 */
typedef enum {
    DEVICE_MODE_NORMAL = 0,     /**< Normal operation */
    DEVICE_MODE_CONFIG,         /**< Configuration mode */
    DEVICE_MODE_LEARNING,       /**< Learning/training mode */
    DEVICE_MODE_LOCKED,         /**< Locked mode */
    DEVICE_MODE_MAX
} device_mode_t;

/**
 * @brief Settings data structure
 * @note All fields are initialized with defaults
 */
typedef struct {
    /* Audio */
    uint8_t     volume;             /**< Audio volume (0-100) */
    bool        sound_enabled;      /**< Sound effects enabled */
    
    /* Display */
    uint8_t     brightness;         /**< Display brightness (0-100) */
    uint8_t     lcd_timeout_sec;    /**< LCD auto-off timeout (seconds, 0=disabled) */
    
    /* System */
    device_mode_t device_mode;      /**< Current device mode */
    bool        power_save;         /**< Power save mode enabled */
    uint16_t    auto_lock_sec;      /**< Auto-lock timeout (seconds, 0=disabled) */
    
    /* Locale */
    char        language[8];        /**< Language code (e.g., "vi", "en") */
    
    /* Statistics (persisted) */
    uint32_t    boot_count;         /**< Number of boots */
    uint32_t    total_runtime_min;  /**< Total runtime in minutes */
    
    /* Flags */
    uint32_t    flags;              /**< General purpose flags */
    
    /* User-defined */
    uint8_t     custom[16];         /**< User-defined settings */
    
} settings_t;

/* ============================================================================
 * Core API
 * ============================================================================ */

/**
 * @brief Initialize settings module
 * @note Must be called after nvs_flash_init()
 * @return ESP_OK on success
 */
esp_err_t settings_init(void);

/**
 * @brief Deinitialize settings module
 */
void settings_deinit(void);

/**
 * @brief Load settings from NVS
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no saved settings
 */
esp_err_t settings_load(void);

/**
 * @brief Save settings to NVS
 * @return ESP_OK on success
 */
esp_err_t settings_save(void);

/**
 * @brief Reset all settings to defaults
 * @param save If true, save to NVS immediately
 * @return ESP_OK on success
 */
esp_err_t settings_reset(bool save);

/**
 * @brief Get pointer to current settings (read-only)
 * @return Pointer to settings structure
 */
const settings_t* settings_get(void);

/**
 * @brief Get mutable pointer to settings
 * @note Call settings_save() after modifications
 * @return Pointer to settings structure
 */
settings_t* settings_get_mutable(void);

/* ============================================================================
 * Audio Settings
 * ============================================================================ */

/**
 * @brief Set volume level
 * @param volume Volume level (0-100)
 */
void settings_set_volume(uint8_t volume);

/**
 * @brief Get current volume level
 * @return Volume level (0-100)
 */
uint8_t settings_get_volume(void);

/**
 * @brief Enable/disable sound effects
 * @param enabled true to enable
 */
void settings_set_sound_enabled(bool enabled);

/**
 * @brief Check if sound is enabled
 * @return true if enabled
 */
bool settings_get_sound_enabled(void);

/* ============================================================================
 * Display Settings
 * ============================================================================ */

/**
 * @brief Set display brightness
 * @param brightness Brightness level (0-100)
 */
void settings_set_brightness(uint8_t brightness);

/**
 * @brief Get current brightness
 * @return Brightness level (0-100)
 */
uint8_t settings_get_brightness(void);

/**
 * @brief Set LCD timeout
 * @param timeout_sec Timeout in seconds (0 = disabled)
 */
void settings_set_lcd_timeout(uint8_t timeout_sec);

/**
 * @brief Get LCD timeout
 * @return Timeout in seconds
 */
uint8_t settings_get_lcd_timeout(void);

/* ============================================================================
 * System Settings
 * ============================================================================ */

/**
 * @brief Set device mode
 * @param mode Device operating mode
 */
void settings_set_device_mode(device_mode_t mode);

/**
 * @brief Get current device mode
 * @return Current device mode
 */
device_mode_t settings_get_device_mode(void);

/**
 * @brief Set power save mode
 * @param enabled true to enable
 */
void settings_set_power_save(bool enabled);

/**
 * @brief Check if power save is enabled
 * @return true if enabled
 */
bool settings_get_power_save(void);

/**
 * @brief Set auto-lock timeout
 * @param timeout_sec Timeout in seconds (0 = disabled)
 */
void settings_set_auto_lock(uint16_t timeout_sec);

/**
 * @brief Get auto-lock timeout
 * @return Timeout in seconds
 */
uint16_t settings_get_auto_lock(void);

/* ============================================================================
 * Locale Settings
 * ============================================================================ */

/**
 * @brief Set language
 * @param lang Language code (e.g., "vi", "en")
 */
void settings_set_language(const char *lang);

/**
 * @brief Get current language
 * @return Language code string
 */
const char* settings_get_language(void);

/* ============================================================================
 * Flags
 * ============================================================================ */

/**
 * @brief Set a flag
 * @param bit Flag bit position (0-31)
 * @param value Flag value
 */
void settings_set_flag(uint8_t bit, bool value);

/**
 * @brief Get a flag
 * @param bit Flag bit position (0-31)
 * @return Flag value
 */
bool settings_get_flag(uint8_t bit);

/**
 * @brief Set all flags
 * @param flags Flag bitmask
 */
void settings_set_flags(uint32_t flags);

/**
 * @brief Get all flags
 * @return Flag bitmask
 */
uint32_t settings_get_flags(void);

/* ============================================================================
 * Statistics
 * ============================================================================ */

/**
 * @brief Get boot count
 * @return Number of boots
 */
uint32_t settings_get_boot_count(void);

/**
 * @brief Increment boot count (called on init)
 */
void settings_increment_boot_count(void);

/**
 * @brief Update runtime statistics
 * @param minutes Minutes to add
 */
void settings_add_runtime(uint32_t minutes);

/**
 * @brief Get total runtime
 * @return Total runtime in minutes
 */
uint32_t settings_get_runtime(void);

/* ============================================================================
 * Custom Data
 * ============================================================================ */

/**
 * @brief Set custom data byte
 * @param index Byte index (0-15)
 * @param value Byte value
 */
void settings_set_custom(uint8_t index, uint8_t value);

/**
 * @brief Get custom data byte
 * @param index Byte index (0-15)
 * @return Byte value
 */
uint8_t settings_get_custom(uint8_t index);

/**
 * @brief Set custom data block
 * @param data Data buffer (max 16 bytes)
 * @param len Data length
 */
void settings_set_custom_data(const uint8_t *data, size_t len);

/**
 * @brief Get custom data block
 * @param data Output buffer
 * @param len Buffer length
 * @return Actual data length copied
 */
size_t settings_get_custom_data(uint8_t *data, size_t len);

/* ============================================================================
 * Debug
 * ============================================================================ */

/**
 * @brief Print current settings to log
 */
void settings_dump(void);

#ifdef __cplusplus
}
#endif

#endif /* SETTINGS_H */
