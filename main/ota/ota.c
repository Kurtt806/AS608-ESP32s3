/**
 * @file ota.c
 * @brief OTA (Over-The-Air) Update Module Implementation
 */

#include "ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "OTA";

/* ============================================================================
 * Private Data
 * ============================================================================ */

/** @brief OTA handle for chunked updates */
static esp_ota_handle_t s_ota_handle = 0;

/** @brief Target partition for update */
static const esp_partition_t *s_update_partition = NULL;

/** @brief Current progress */
static ota_progress_t s_progress = {0};

/** @brief Mutex for thread safety */
static SemaphoreHandle_t s_mutex = NULL;

/** @brief Progress callback */
static ota_progress_cb_t s_callback = NULL;
static void *s_callback_user_data = NULL;

/** @brief Initialization flag */
static bool s_initialized = false;

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

static void update_progress(ota_state_t state, const char *message, int error_code) {
    lock();
    
    s_progress.state = state;
    s_progress.message = message;
    s_progress.error_code = error_code;
    
    if (s_progress.total_size > 0) {
        s_progress.progress = (uint8_t)((s_progress.received_size * 100) / s_progress.total_size);
    }
    
    /* Call callback */
    if (s_callback) {
        s_callback(&s_progress, s_callback_user_data);
    }
    
    unlock();
}

static void reset_progress(void) {
    lock();
    memset(&s_progress, 0, sizeof(s_progress));
    s_progress.state = OTA_STATE_IDLE;
    s_progress.message = "Ready";
    unlock();
}

/* ============================================================================
 * Core API
 * ============================================================================ */

esp_err_t ota_init(void) {
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
    
    reset_progress();
    
    /* Check if we need to validate current firmware */
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGW(TAG, "Firmware pending verification - call ota_mark_valid() after successful boot");
        }
    }
    
    s_initialized = true;
    ESP_LOGI(TAG, "Initialized");
    
    return ESP_OK;
}

void ota_deinit(void) {
    if (!s_initialized) return;
    
    if (s_ota_handle) {
        esp_ota_abort(s_ota_handle);
        s_ota_handle = 0;
    }
    
    if (s_mutex) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
    
    s_initialized = false;
    ESP_LOGI(TAG, "Deinitialized");
}

ota_state_t ota_get_state(void) {
    return s_progress.state;
}

esp_err_t ota_get_progress(ota_progress_t *progress) {
    if (!progress) return ESP_ERR_INVALID_ARG;
    
    lock();
    memcpy(progress, &s_progress, sizeof(ota_progress_t));
    unlock();
    
    return ESP_OK;
}

void ota_set_progress_callback(ota_progress_cb_t callback, void *user_data) {
    lock();
    s_callback = callback;
    s_callback_user_data = user_data;
    unlock();
}

/* ============================================================================
 * Firmware Info API
 * ============================================================================ */

esp_err_t ota_get_firmware_info(ota_firmware_info_t *info) {
    if (!info) return ESP_ERR_INVALID_ARG;
    
    memset(info, 0, sizeof(ota_firmware_info_t));
    
    /* Get app description */
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc) {
        strncpy(info->version, app_desc->version, sizeof(info->version) - 1);
        strncpy(info->project_name, app_desc->project_name, sizeof(info->project_name) - 1);
        strncpy(info->compile_date, app_desc->date, sizeof(info->compile_date) - 1);
        strncpy(info->compile_time, app_desc->time, sizeof(info->compile_time) - 1);
        strncpy(info->idf_version, app_desc->idf_ver, sizeof(info->idf_version) - 1);
    }
    
    /* Get running partition info */
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running) {
        info->app_size = running->size;
        info->is_factory = (running->subtype == ESP_PARTITION_SUBTYPE_APP_FACTORY);
    }
    
    /* Check rollback availability */
    const esp_partition_t *last_invalid = esp_ota_get_last_invalid_partition();
    info->can_rollback = (last_invalid != NULL);
    
    /* Also check if there's a valid previous OTA partition */
    if (!info->can_rollback) {
        esp_ota_img_states_t ota_state;
        const esp_partition_t *boot_partition = esp_ota_get_boot_partition();
        if (boot_partition && boot_partition != running) {
            if (esp_ota_get_state_partition(boot_partition, &ota_state) == ESP_OK) {
                info->can_rollback = (ota_state == ESP_OTA_IMG_VALID);
            }
        }
    }
    
    return ESP_OK;
}

const char* ota_get_running_partition(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    return running ? running->label : "unknown";
}

/* ============================================================================
 * Update API - Chunked Upload
 * ============================================================================ */

esp_err_t ota_begin(uint32_t total_size) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    
    /* Auto-abort previous failed/stuck OTA session */
    if (s_progress.state != OTA_STATE_IDLE) {
        ESP_LOGW(TAG, "Previous OTA session not idle (state=%d), aborting...", s_progress.state);
        ota_abort();
    }
    
    /* Find next OTA partition */
    s_update_partition = esp_ota_get_next_update_partition(NULL);
    if (!s_update_partition) {
        ESP_LOGE(TAG, "No OTA partition found");
        update_progress(OTA_STATE_ERROR, "No OTA partition", ESP_ERR_NOT_FOUND);
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Writing to partition: %s", s_update_partition->label);
    
    /* Begin OTA */
    esp_err_t ret = esp_ota_begin(s_update_partition, OTA_SIZE_UNKNOWN, &s_ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(ret));
        update_progress(OTA_STATE_ERROR, "Begin failed", ret);
        return ret;
    }
    
    /* Reset progress */
    lock();
    s_progress.state = OTA_STATE_DOWNLOADING;
    s_progress.total_size = total_size;
    s_progress.received_size = 0;
    s_progress.progress = 0;
    s_progress.message = "Receiving firmware...";
    s_progress.error_code = 0;
    unlock();
    
    update_progress(OTA_STATE_DOWNLOADING, "Receiving firmware...", 0);
    
    return ESP_OK;
}

esp_err_t ota_write(const void *data, size_t len) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!s_ota_handle) return ESP_ERR_INVALID_STATE;
    if (!data || len == 0) return ESP_ERR_INVALID_ARG;
    
    esp_err_t ret = esp_ota_write(s_ota_handle, data, len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(ret));
        update_progress(OTA_STATE_ERROR, "Write failed", ret);
        return ret;
    }
    
    lock();
    s_progress.received_size += len;
    if (s_progress.total_size > 0) {
        s_progress.progress = (uint8_t)((s_progress.received_size * 100) / s_progress.total_size);
    }
    unlock();
    
    /* Log progress every 10% */
    static uint8_t last_logged = 0;
    if (s_progress.progress / 10 > last_logged / 10) {
        last_logged = s_progress.progress;
        ESP_LOGI(TAG, "Progress: %d%% (%lu / %lu bytes)", 
                 s_progress.progress, 
                 (unsigned long)s_progress.received_size, 
                 (unsigned long)s_progress.total_size);
    }
    
    return ESP_OK;
}

esp_err_t ota_end(void) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!s_ota_handle) return ESP_ERR_INVALID_STATE;
    
    update_progress(OTA_STATE_VERIFYING, "Verifying firmware...", 0);
    
    /* End OTA (validates the firmware) */
    esp_err_t ret = esp_ota_end(s_ota_handle);
    s_ota_handle = 0;
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(ret));
        update_progress(OTA_STATE_ERROR, "Verification failed", ret);
        return ret;
    }
    
    update_progress(OTA_STATE_APPLYING, "Setting boot partition...", 0);
    
    /* Set boot partition */
    ret = esp_ota_set_boot_partition(s_update_partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(ret));
        update_progress(OTA_STATE_ERROR, "Set boot failed", ret);
        return ret;
    }
    
    update_progress(OTA_STATE_COMPLETE, "Update complete! Reboot to apply.", 0);
    ESP_LOGI(TAG, "OTA update complete. Reboot to apply.");
    
    return ESP_OK;
}

esp_err_t ota_abort(void) {
    if (s_ota_handle) {
        esp_ota_abort(s_ota_handle);
        s_ota_handle = 0;
    }
    
    reset_progress();
    ESP_LOGI(TAG, "OTA aborted");
    
    return ESP_OK;
}

/* ============================================================================
 * Update API - URL Download
 * ============================================================================ */

static void ota_url_task(void *pvParameters) {
    char *url = (char *)pvParameters;
    
    update_progress(OTA_STATE_STARTING, "Connecting...", 0);
    
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = OTA_TIMEOUT_SEC * 1000,
        .keep_alive_enable = true,
    };
    
    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };
    
    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t ret = esp_https_ota_begin(&ota_config, &https_ota_handle);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_begin failed: %s", esp_err_to_name(ret));
        update_progress(OTA_STATE_ERROR, "Connection failed", ret);
        free(url);
        vTaskDelete(NULL);
        return;
    }
    
    /* Get image size */
    int total_size = esp_https_ota_get_image_size(https_ota_handle);
    lock();
    s_progress.total_size = (total_size > 0) ? total_size : 0;
    unlock();
    
    update_progress(OTA_STATE_DOWNLOADING, "Downloading...", 0);
    
    /* Download and flash */
    while (1) {
        ret = esp_https_ota_perform(https_ota_handle);
        if (ret != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        
        /* Update progress */
        int read_len = esp_https_ota_get_image_len_read(https_ota_handle);
        lock();
        s_progress.received_size = read_len;
        if (s_progress.total_size > 0) {
            s_progress.progress = (uint8_t)((read_len * 100) / s_progress.total_size);
        }
        unlock();
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_perform failed: %s", esp_err_to_name(ret));
        esp_https_ota_abort(https_ota_handle);
        update_progress(OTA_STATE_ERROR, "Download failed", ret);
        free(url);
        vTaskDelete(NULL);
        return;
    }
    
    /* Verify and finish */
    if (!esp_https_ota_is_complete_data_received(https_ota_handle)) {
        ESP_LOGE(TAG, "Incomplete data received");
        esp_https_ota_abort(https_ota_handle);
        update_progress(OTA_STATE_ERROR, "Incomplete download", ESP_ERR_INVALID_SIZE);
        free(url);
        vTaskDelete(NULL);
        return;
    }
    
    update_progress(OTA_STATE_VERIFYING, "Verifying...", 0);
    
    ret = esp_https_ota_finish(https_ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_finish failed: %s", esp_err_to_name(ret));
        update_progress(OTA_STATE_ERROR, "Verification failed", ret);
        free(url);
        vTaskDelete(NULL);
        return;
    }
    
    update_progress(OTA_STATE_COMPLETE, "Update complete! Reboot to apply.", 0);
    ESP_LOGI(TAG, "OTA from URL complete");
    
    free(url);
    vTaskDelete(NULL);
}

esp_err_t ota_start_url(const char *url) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!url || strlen(url) == 0) return ESP_ERR_INVALID_ARG;
    
    if (s_progress.state != OTA_STATE_IDLE) {
        ESP_LOGE(TAG, "Update already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    
    /* Copy URL for task */
    char *url_copy = strdup(url);
    if (!url_copy) {
        return ESP_ERR_NO_MEM;
    }
    
    /* Create download task */
    BaseType_t ret = xTaskCreate(
        ota_url_task,
        "ota_url",
        8192,
        url_copy,
        5,
        NULL
    );
    
    if (ret != pdPASS) {
        free(url_copy);
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

/* ============================================================================
 * Post-Update API
 * ============================================================================ */

esp_err_t ota_mark_valid(void) {
    esp_err_t ret = esp_ota_mark_app_valid_cancel_rollback();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Firmware marked as valid");
    }
    return ret;
}

esp_err_t ota_rollback(void) {
    esp_err_t ret = esp_ota_mark_app_invalid_rollback_and_reboot();
    /* This function does not return on success */
    return ret;
}

void ota_reboot(void) {
    ESP_LOGI(TAG, "Rebooting...");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

bool ota_is_pending_verify(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        return (ota_state == ESP_OTA_IMG_PENDING_VERIFY);
    }
    
    return false;
}
