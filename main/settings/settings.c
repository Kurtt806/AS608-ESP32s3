/**
 * @file settings.c
 * @brief Settings Module Implementation
 * 
 * Persistent settings storage using ESP-IDF NVS (Non-Volatile Storage).
 * Thread-safe with mutex protection.
 */

#include "settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "SETTINGS";

/* ============================================================================
 * Private Data
 * ============================================================================ */

/** @brief NVS key for settings blob */
#define NVS_KEY_SETTINGS    "cfg"

/** @brief Settings version for migration support */
#define SETTINGS_VERSION    1

/** @brief Stored settings with version header */
typedef struct {
    uint8_t     version;
    uint8_t     reserved[3];
    settings_t  data;
} settings_storage_t;

/** @brief Current settings in RAM */
static settings_storage_t s_storage = {0};

/** @brief NVS handle */
static nvs_handle_t s_nvs = 0;

/** @brief Mutex for thread safety */
static SemaphoreHandle_t s_mutex = NULL;

/** @brief Initialization flag */
static bool s_initialized = false;

/* ============================================================================
 * Private Functions
 * ============================================================================ */

static void settings_set_defaults(settings_t *s) {
    memset(s, 0, sizeof(settings_t));
    
    /* Audio */
    s->volume = SETTINGS_DEFAULT_VOLUME;
    s->sound_enabled = SETTINGS_DEFAULT_SOUND_ENABLED;
    
    /* Display */
    s->brightness = SETTINGS_DEFAULT_BRIGHTNESS;
    s->lcd_timeout_sec = SETTINGS_DEFAULT_LCD_TIMEOUT;
    
    /* System */
    s->device_mode = SETTINGS_DEFAULT_DEVICE_MODE;
    s->power_save = SETTINGS_DEFAULT_POWER_SAVE;
    s->auto_lock_sec = SETTINGS_DEFAULT_AUTO_LOCK_SEC;
    
    /* Locale */
    strncpy(s->language, SETTINGS_DEFAULT_LANGUAGE, sizeof(s->language) - 1);
    
    /* Statistics */
    s->boot_count = 0;
    s->total_runtime_min = 0;
    
    /* Flags */
    s->flags = 0;
}

static inline void lock(void) {
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

static inline void unlock(void) {
    if (s_mutex) {
        xSemaphoreGive(s_mutex);
    }
}

static inline void auto_save(void) {
#if SETTINGS_AUTO_SAVE
    settings_save();
#endif
}

/* ============================================================================
 * Core API
 * ============================================================================ */

esp_err_t settings_init(void) {
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }
    
    /* Create mutex */
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    /* Open NVS namespace */
    esp_err_t ret = nvs_open(SETTINGS_NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ret;
    }
    
    /* Initialize with defaults */
    s_storage.version = SETTINGS_VERSION;
    settings_set_defaults(&s_storage.data);
    
    /* Try to load saved settings */
    ret = settings_load();
    if (ret == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved settings, using defaults");
        settings_save();  /* Save defaults */
    } else if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load settings, using defaults");
    }
    
    /* Increment boot count */
    settings_increment_boot_count();
    
    s_initialized = true;
    ESP_LOGI(TAG, "Initialized (boot #%lu)", (unsigned long)s_storage.data.boot_count);
    
    return ESP_OK;
}

void settings_deinit(void) {
    if (!s_initialized) return;
    
    /* Save before closing */
    settings_save();
    
    if (s_nvs) {
        nvs_close(s_nvs);
        s_nvs = 0;
    }
    
    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
    
    s_initialized = false;
    ESP_LOGI(TAG, "Deinitialized");
}

esp_err_t settings_load(void) {
    if (!s_nvs) return ESP_ERR_INVALID_STATE;
    
    lock();
    
    size_t size = sizeof(settings_storage_t);
    esp_err_t ret = nvs_get_blob(s_nvs, NVS_KEY_SETTINGS, &s_storage, &size);
    
    if (ret == ESP_OK) {
        /* Version migration if needed */
        if (s_storage.version < SETTINGS_VERSION) {
            ESP_LOGI(TAG, "Migrating settings v%d -> v%d", 
                     s_storage.version, SETTINGS_VERSION);
            /* Add migration logic here for future versions */
            s_storage.version = SETTINGS_VERSION;
        }
        ESP_LOGI(TAG, "Loaded settings from NVS");
    } else if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ret = ESP_ERR_NOT_FOUND;
    } else {
        ESP_LOGE(TAG, "nvs_get_blob failed: %s", esp_err_to_name(ret));
    }
    
    unlock();
    return ret;
}

esp_err_t settings_save(void) {
    if (!s_nvs) return ESP_ERR_INVALID_STATE;
    
    lock();
    
    esp_err_t ret = nvs_set_blob(s_nvs, NVS_KEY_SETTINGS, &s_storage, sizeof(s_storage));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_blob failed: %s", esp_err_to_name(ret));
        unlock();
        return ret;
    }
    
    ret = nvs_commit(s_nvs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGD(TAG, "Settings saved");
    }
    
    unlock();
    return ret;
}

esp_err_t settings_reset(bool save) {
    lock();
    
    settings_set_defaults(&s_storage.data);
    ESP_LOGI(TAG, "Settings reset to defaults");
    
    unlock();
    
    if (save) {
        return settings_save();
    }
    return ESP_OK;
}

const settings_t* settings_get(void) {
    return &s_storage.data;
}

settings_t* settings_get_mutable(void) {
    return &s_storage.data;
}

/* ============================================================================
 * Audio Settings
 * ============================================================================ */

void settings_set_volume(uint8_t volume) {
    lock();
    s_storage.data.volume = (volume > 100) ? 100 : volume;
    unlock();
    auto_save();
}

uint8_t settings_get_volume(void) {
    return s_storage.data.volume;
}

void settings_set_sound_enabled(bool enabled) {
    lock();
    s_storage.data.sound_enabled = enabled;
    unlock();
    auto_save();
}

bool settings_get_sound_enabled(void) {
    return s_storage.data.sound_enabled;
}

/* ============================================================================
 * Display Settings
 * ============================================================================ */

void settings_set_brightness(uint8_t brightness) {
    lock();
    s_storage.data.brightness = (brightness > 100) ? 100 : brightness;
    unlock();
    auto_save();
}

uint8_t settings_get_brightness(void) {
    return s_storage.data.brightness;
}

void settings_set_lcd_timeout(uint8_t timeout_sec) {
    lock();
    s_storage.data.lcd_timeout_sec = timeout_sec;
    unlock();
    auto_save();
}

uint8_t settings_get_lcd_timeout(void) {
    return s_storage.data.lcd_timeout_sec;
}

/* ============================================================================
 * System Settings
 * ============================================================================ */

void settings_set_device_mode(device_mode_t mode) {
    if (mode >= DEVICE_MODE_MAX) return;
    
    lock();
    s_storage.data.device_mode = mode;
    unlock();
    auto_save();
}

device_mode_t settings_get_device_mode(void) {
    return s_storage.data.device_mode;
}

void settings_set_power_save(bool enabled) {
    lock();
    s_storage.data.power_save = enabled;
    unlock();
    auto_save();
}

bool settings_get_power_save(void) {
    return s_storage.data.power_save;
}

void settings_set_auto_lock(uint16_t timeout_sec) {
    lock();
    s_storage.data.auto_lock_sec = timeout_sec;
    unlock();
    auto_save();
}

uint16_t settings_get_auto_lock(void) {
    return s_storage.data.auto_lock_sec;
}

/* ============================================================================
 * Locale Settings
 * ============================================================================ */

void settings_set_language(const char *lang) {
    if (!lang) return;
    
    lock();
    strncpy(s_storage.data.language, lang, sizeof(s_storage.data.language) - 1);
    s_storage.data.language[sizeof(s_storage.data.language) - 1] = '\0';
    unlock();
    auto_save();
}

const char* settings_get_language(void) {
    return s_storage.data.language;
}

/* ============================================================================
 * Flags
 * ============================================================================ */

void settings_set_flag(uint8_t bit, bool value) {
    if (bit >= 32) return;
    
    lock();
    if (value) {
        s_storage.data.flags |= (1UL << bit);
    } else {
        s_storage.data.flags &= ~(1UL << bit);
    }
    unlock();
    auto_save();
}

bool settings_get_flag(uint8_t bit) {
    if (bit >= 32) return false;
    return (s_storage.data.flags & (1UL << bit)) != 0;
}

void settings_set_flags(uint32_t flags) {
    lock();
    s_storage.data.flags = flags;
    unlock();
    auto_save();
}

uint32_t settings_get_flags(void) {
    return s_storage.data.flags;
}

/* ============================================================================
 * Statistics
 * ============================================================================ */

uint32_t settings_get_boot_count(void) {
    return s_storage.data.boot_count;
}

void settings_increment_boot_count(void) {
    lock();
    s_storage.data.boot_count++;
    unlock();
    auto_save();
}

void settings_add_runtime(uint32_t minutes) {
    lock();
    s_storage.data.total_runtime_min += minutes;
    unlock();
    auto_save();
}

uint32_t settings_get_runtime(void) {
    return s_storage.data.total_runtime_min;
}

/* ============================================================================
 * Custom Data
 * ============================================================================ */

void settings_set_custom(uint8_t index, uint8_t value) {
    if (index >= sizeof(s_storage.data.custom)) return;
    
    lock();
    s_storage.data.custom[index] = value;
    unlock();
    auto_save();
}

uint8_t settings_get_custom(uint8_t index) {
    if (index >= sizeof(s_storage.data.custom)) return 0;
    return s_storage.data.custom[index];
}

void settings_set_custom_data(const uint8_t *data, size_t len) {
    if (!data || len == 0) return;
    
    size_t copy_len = (len > sizeof(s_storage.data.custom)) 
                      ? sizeof(s_storage.data.custom) : len;
    
    lock();
    memcpy(s_storage.data.custom, data, copy_len);
    unlock();
    auto_save();
}

size_t settings_get_custom_data(uint8_t *data, size_t len) {
    if (!data || len == 0) return 0;
    
    size_t copy_len = (len > sizeof(s_storage.data.custom)) 
                      ? sizeof(s_storage.data.custom) : len;
    
    lock();
    memcpy(data, s_storage.data.custom, copy_len);
    unlock();
    
    return copy_len;
}

/* ============================================================================
 * Debug
 * ============================================================================ */

void settings_dump(void) {
    const settings_t *s = &s_storage.data;
    
    ESP_LOGI(TAG, "=== Settings Dump ===");
    ESP_LOGI(TAG, "Audio:");
    ESP_LOGI(TAG, "  volume:        %d", s->volume);
    ESP_LOGI(TAG, "  sound_enabled: %s", s->sound_enabled ? "true" : "false");
    ESP_LOGI(TAG, "Display:");
    ESP_LOGI(TAG, "  brightness:    %d", s->brightness);
    ESP_LOGI(TAG, "  lcd_timeout:   %d sec", s->lcd_timeout_sec);
    ESP_LOGI(TAG, "System:");
    ESP_LOGI(TAG, "  device_mode:   %d", s->device_mode);
    ESP_LOGI(TAG, "  power_save:    %s", s->power_save ? "true" : "false");
    ESP_LOGI(TAG, "  auto_lock:     %d sec", s->auto_lock_sec);
    ESP_LOGI(TAG, "Locale:");
    ESP_LOGI(TAG, "  language:      %s", s->language);
    ESP_LOGI(TAG, "Statistics:");
    ESP_LOGI(TAG, "  boot_count:    %lu", (unsigned long)s->boot_count);
    ESP_LOGI(TAG, "  runtime:       %lu min", (unsigned long)s->total_runtime_min);
    ESP_LOGI(TAG, "Flags: 0x%08lX", (unsigned long)s->flags);
    ESP_LOGI(TAG, "=====================");
}
