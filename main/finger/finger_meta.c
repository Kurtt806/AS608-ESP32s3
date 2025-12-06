/**
 * @file finger_meta.c
 * @brief Fingerprint Metadata Management Implementation
 * 
 * Stores fingerprint names and statistics in NVS.
 * Uses compact key format: "fn_XXX" where XXX is the fingerprint ID.
 */

#include "finger_meta.h"
#include "finger.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "FINGER_META";

/* ============================================================================
 * Private Data
 * ============================================================================ */

/** @brief NVS handle */
static nvs_handle_t s_nvs = 0;

/** @brief Mutex for thread safety */
static SemaphoreHandle_t s_mutex = NULL;

/** @brief Initialization flag */
static bool s_initialized = false;

/** @brief In-memory cache for faster access */
static finger_meta_entry_t s_cache[FINGER_META_MAX_COUNT];
static bool s_cache_valid[FINGER_META_MAX_COUNT];

/* ============================================================================
 * Private Functions
 * ============================================================================ */

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

/**
 * @brief Generate NVS key for fingerprint ID
 */
static void make_nvs_key(int id, char *key, size_t key_size) {
    snprintf(key, key_size, "fn_%d", id);
}

/**
 * @brief Get current timestamp in seconds
 */
static uint32_t get_timestamp(void) {
    return (uint32_t)(esp_timer_get_time() / 1000000ULL);
}

/**
 * @brief Load entry from NVS to cache
 */
static esp_err_t load_entry_to_cache(int id) {
    if (id < 0 || id >= FINGER_META_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    
    char key[16];
    make_nvs_key(id, key, sizeof(key));
    
    size_t size = sizeof(finger_meta_entry_t);
    esp_err_t ret = nvs_get_blob(s_nvs, key, &s_cache[id], &size);
    
    if (ret == ESP_OK) {
        s_cache_valid[id] = true;
    } else {
        s_cache_valid[id] = false;
        memset(&s_cache[id], 0, sizeof(finger_meta_entry_t));
    }
    
    return ret;
}

/**
 * @brief Save entry from cache to NVS
 */
static esp_err_t save_entry_from_cache(int id) {
    if (id < 0 || id >= FINGER_META_MAX_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!s_cache_valid[id]) {
        return ESP_ERR_INVALID_STATE;
    }
    
    char key[16];
    make_nvs_key(id, key, sizeof(key));
    
    esp_err_t ret = nvs_set_blob(s_nvs, key, &s_cache[id], sizeof(finger_meta_entry_t));
    if (ret == ESP_OK) {
        ret = nvs_commit(s_nvs);
    }
    
    return ret;
}

/* ============================================================================
 * Core API
 * ============================================================================ */

esp_err_t finger_meta_init(void) {
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
    esp_err_t ret = nvs_open(FINGER_META_NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        return ret;
    }
    
    /* Initialize cache */
    memset(s_cache, 0, sizeof(s_cache));
    memset(s_cache_valid, 0, sizeof(s_cache_valid));
    
    /* Load all existing entries to cache */
    int loaded = 0;
    for (int i = 0; i < FINGER_META_MAX_COUNT; i++) {
        if (load_entry_to_cache(i) == ESP_OK) {
            loaded++;
        }
    }
    
    s_initialized = true;
    ESP_LOGI(TAG, "Initialized (%d entries loaded)", loaded);
    
    return ESP_OK;
}

void finger_meta_deinit(void) {
    if (!s_initialized) return;
    
    lock();
    
    if (s_nvs) {
        nvs_close(s_nvs);
        s_nvs = 0;
    }
    
    unlock();
    
    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
    
    s_initialized = false;
    ESP_LOGI(TAG, "Deinitialized");
}

/* ============================================================================
 * Name Management
 * ============================================================================ */

esp_err_t finger_meta_set_name(int id, const char *name) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (id < 0 || id >= FINGER_META_MAX_COUNT) return ESP_ERR_INVALID_ARG;
    if (!name) return ESP_ERR_INVALID_ARG;
    
    lock();
    
    /* Create or update entry */
    if (!s_cache_valid[id]) {
        /* New entry */
        memset(&s_cache[id], 0, sizeof(finger_meta_entry_t));
        s_cache[id].id = id;
        s_cache[id].created_at = get_timestamp();
        s_cache_valid[id] = true;
    }
    
    /* Update name */
    strncpy(s_cache[id].name, name, FINGER_NAME_MAX_LEN - 1);
    s_cache[id].name[FINGER_NAME_MAX_LEN - 1] = '\0';
    
    esp_err_t ret = save_entry_from_cache(id);
    
    unlock();
    
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Set name for ID %d: %s", id, name);
    } else {
        ESP_LOGE(TAG, "Failed to save name for ID %d: %s", id, esp_err_to_name(ret));
    }
    
    return ret;
}

const char* finger_meta_get_name(int id) {
    if (!s_initialized) return NULL;
    if (id < 0 || id >= FINGER_META_MAX_COUNT) return NULL;
    
    lock();
    
    const char *result = NULL;
    if (s_cache_valid[id] && s_cache[id].name[0] != '\0') {
        result = s_cache[id].name;
    }
    
    unlock();
    
    return result;
}

bool finger_meta_has_name(int id) {
    return finger_meta_get_name(id) != NULL;
}

esp_err_t finger_meta_delete_name(int id) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (id < 0 || id >= FINGER_META_MAX_COUNT) return ESP_ERR_INVALID_ARG;
    
    lock();
    
    char key[16];
    make_nvs_key(id, key, sizeof(key));
    
    esp_err_t ret = nvs_erase_key(s_nvs, key);
    if (ret == ESP_OK || ret == ESP_ERR_NVS_NOT_FOUND) {
        nvs_commit(s_nvs);
        s_cache_valid[id] = false;
        memset(&s_cache[id], 0, sizeof(finger_meta_entry_t));
        ret = ESP_OK;
    }
    
    unlock();
    
    ESP_LOGI(TAG, "Deleted metadata for ID %d", id);
    return ret;
}

esp_err_t finger_meta_clear_all(void) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    
    lock();
    
    /* Erase all entries from NVS */
    esp_err_t ret = nvs_erase_all(s_nvs);
    if (ret == ESP_OK) {
        ret = nvs_commit(s_nvs);
    }
    
    /* Clear cache */
    memset(s_cache, 0, sizeof(s_cache));
    memset(s_cache_valid, 0, sizeof(s_cache_valid));
    
    unlock();
    
    ESP_LOGI(TAG, "Cleared all metadata");
    return ret;
}

/* ============================================================================
 * Full Metadata Management
 * ============================================================================ */

esp_err_t finger_meta_get_entry(int id, finger_meta_entry_t *entry) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (id < 0 || id >= FINGER_META_MAX_COUNT) return ESP_ERR_INVALID_ARG;
    if (!entry) return ESP_ERR_INVALID_ARG;
    
    lock();
    
    esp_err_t ret;
    if (s_cache_valid[id]) {
        memcpy(entry, &s_cache[id], sizeof(finger_meta_entry_t));
        ret = ESP_OK;
    } else {
        ret = ESP_ERR_NOT_FOUND;
    }
    
    unlock();
    
    return ret;
}

esp_err_t finger_meta_set_entry(int id, const finger_meta_entry_t *entry) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (id < 0 || id >= FINGER_META_MAX_COUNT) return ESP_ERR_INVALID_ARG;
    if (!entry) return ESP_ERR_INVALID_ARG;
    
    lock();
    
    memcpy(&s_cache[id], entry, sizeof(finger_meta_entry_t));
    s_cache[id].id = id;  /* Ensure ID matches */
    s_cache_valid[id] = true;
    
    esp_err_t ret = save_entry_from_cache(id);
    
    unlock();
    
    return ret;
}

esp_err_t finger_meta_record_match(int id) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (id < 0 || id >= FINGER_META_MAX_COUNT) return ESP_ERR_INVALID_ARG;
    
    lock();
    
    esp_err_t ret;
    
    if (!s_cache_valid[id]) {
        /* Create entry if not exists */
        memset(&s_cache[id], 0, sizeof(finger_meta_entry_t));
        s_cache[id].id = id;
        s_cache[id].created_at = get_timestamp();
        s_cache_valid[id] = true;
    }
    
    s_cache[id].last_match = get_timestamp();
    s_cache[id].match_count++;
    
    ret = save_entry_from_cache(id);
    
    unlock();
    
    ESP_LOGD(TAG, "Recorded match for ID %d (count: %d)", id, s_cache[id].match_count);
    return ret;
}

esp_err_t finger_meta_create(int id, const char *name) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (id < 0 || id >= FINGER_META_MAX_COUNT) return ESP_ERR_INVALID_ARG;
    
    lock();
    
    /* Initialize new entry */
    memset(&s_cache[id], 0, sizeof(finger_meta_entry_t));
    s_cache[id].id = id;
    s_cache[id].created_at = get_timestamp();
    s_cache[id].last_match = 0;
    s_cache[id].match_count = 0;
    
    /* Set name */
    if (name && name[0] != '\0') {
        strncpy(s_cache[id].name, name, FINGER_NAME_MAX_LEN - 1);
    } else {
        /* Auto-generate name: "ID_XXX" */
        snprintf(s_cache[id].name, FINGER_NAME_MAX_LEN, "ID_%d", id);
    }
    s_cache[id].name[FINGER_NAME_MAX_LEN - 1] = '\0';
    
    s_cache_valid[id] = true;
    
    esp_err_t ret = save_entry_from_cache(id);
    
    unlock();
    
    ESP_LOGI(TAG, "Created metadata for ID %d: %s", id, s_cache[id].name);
    return ret;
}

/* ============================================================================
 * Query API
 * ============================================================================ */

int finger_meta_count(void) {
    if (!s_initialized) return 0;
    
    int count = 0;
    
    lock();
    
    for (int i = 0; i < FINGER_META_MAX_COUNT; i++) {
        if (s_cache_valid[i]) {
            count++;
        }
    }
    
    unlock();
    
    return count;
}

void finger_meta_iterate(finger_meta_iterate_cb callback, void *user_data) {
    if (!s_initialized || !callback) return;
    
    lock();
    
    for (int i = 0; i < FINGER_META_MAX_COUNT; i++) {
        if (s_cache_valid[i]) {
            if (!callback(&s_cache[i], user_data)) {
                break;  /* Stop iteration if callback returns false */
            }
        }
    }
    
    unlock();
}
