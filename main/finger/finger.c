/**
 * @file finger.c
 * @brief Fingerprint Module Implementation
 */

#include "finger.h"
#include "finger_events.h"
#include "../common/config.h"
#include "as608.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "FINGER";

ESP_EVENT_DEFINE_BASE(FINGER_EVENT);

typedef enum {
    STATE_IDLE = 0,
    STATE_SEARCHING,
    STATE_ENROLL_STEP1,
    STATE_ENROLL_STEP2,
    STATE_ENROLL_STORE,
} task_state_t;

typedef struct {
    finger_cmd_t cmd;
    int16_t param;
} cmd_msg_t;

static TaskHandle_t s_task = NULL;
static QueueHandle_t s_queue = NULL;
static task_state_t s_state = STATE_IDLE;
static int16_t s_enroll_id = -1;
static bool s_finger_on = false;
static uint16_t s_template_count = 0;

static inline void post_event(finger_event_id_t id, void *data, size_t len) {
    esp_event_post(FINGER_EVENT, id, data, len, portMAX_DELAY);
}

/**
 * @brief Find the first free slot in fingerprint library
 * @return Free slot ID (0 to capacity-1), or -1 if library is full
 * 
 * Strategy: Use linear search from 0. For a more accurate approach,
 * we could read the index table from AS608, but this simple method works
 * for sequential enrollment.
 */
static int16_t find_free_slot(void) {
    /* Simple approach: use template_count as next ID
     * This works if templates are stored sequentially without gaps.
     * For production, consider reading AS608 index table to find gaps.
     */
    if (s_template_count >= CFG_AS608_LIBRARY_SIZE) {
        ESP_LOGW(TAG, "Library full! %d/%d", s_template_count, CFG_AS608_LIBRARY_SIZE);
        return -1;
    }
    
    /* 
     * AS608 uses 0-based indexing for PageID.
     * If we have N templates (0 to N-1), next free slot is N.
     */
    ESP_LOGI(TAG, "Auto-find slot: using ID %d (templates=%d, capacity=%d)",
             s_template_count, s_template_count, CFG_AS608_LIBRARY_SIZE);
    
    return (int16_t)s_template_count;
}

static void process_cmd(const cmd_msg_t *msg) {
    switch (msg->cmd) {
        case FINGER_CMD_IDLE:
            ESP_LOGI(TAG, "CMD: IDLE");
            s_state = STATE_IDLE;
            s_enroll_id = -1;
            break;

        case FINGER_CMD_SEARCH:
            ESP_LOGI(TAG, "CMD: SEARCH");
            s_state = STATE_SEARCHING;
            break;

        case FINGER_CMD_ENROLL:
            ESP_LOGI(TAG, "CMD: ENROLL requested_id=%d", msg->param);
            
            /* Handle auto-find slot when param < 0 */
            if (msg->param < 0) {
                s_enroll_id = find_free_slot();
                if (s_enroll_id < 0) {
                    ESP_LOGE(TAG, "Cannot enroll: library full!");
                    post_event(FINGER_EVT_ENROLL_FAIL, NULL, 0);
                    s_state = STATE_IDLE;
                    return;
                }
                ESP_LOGI(TAG, "Auto-assigned slot: %d", s_enroll_id);
            } else {
                /* Validate explicit ID */
                if (msg->param >= CFG_AS608_LIBRARY_SIZE) {
                    ESP_LOGE(TAG, "Invalid enroll ID %d (max=%d)", 
                             msg->param, CFG_AS608_LIBRARY_SIZE - 1);
                    post_event(FINGER_EVT_ENROLL_FAIL, NULL, 0);
                    s_state = STATE_IDLE;
                    return;
                }
                s_enroll_id = msg->param;
            }
            
            s_state = STATE_ENROLL_STEP1;
            ESP_LOGI(TAG, "Starting enroll for ID %d", s_enroll_id);
            {
                finger_enroll_data_t data = { .finger_id = s_enroll_id, .step = 1 };
                post_event(FINGER_EVT_ENROLL_START, &data, sizeof(data));
            }
            break;

        case FINGER_CMD_CANCEL:
            ESP_LOGI(TAG, "CMD: CANCEL");
            if (s_state != STATE_IDLE) {
                post_event(FINGER_EVT_ENROLL_CANCEL, NULL, 0);
            }
            s_state = STATE_IDLE;
            s_enroll_id = -1;
            break;
    }
}

static void wait_finger_remove(void) {
    ESP_LOGD(TAG, "Waiting for finger removal...");
    int cnt = 0;
    while (as608_read_image() == ESP_OK && cnt < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        cnt++;
    }
    s_finger_on = false;
    ESP_LOGD(TAG, "Finger removed");
}

static void do_search(void) {
    esp_err_t ret = as608_read_image();
    
    if (ret == ESP_ERR_NOT_FOUND) {
        /* No finger detected */
        if (s_finger_on) {
            s_finger_on = false;
            ESP_LOGD(TAG, "Finger removed");
            post_event(FINGER_EVT_REMOVED, NULL, 0);
        }
        return;
    }
    
    if (ret != ESP_OK) {
        /* Communication error - log but don't spam */
        ESP_LOGD(TAG, "read_image error: %s", esp_err_to_name(ret));
        return;
    }

    /* Finger detected and image captured */
    if (!s_finger_on) {
        s_finger_on = true;
        ESP_LOGI(TAG, "Finger detected!");
        post_event(FINGER_EVT_DETECTED, NULL, 0);
    }

    ret = as608_gen_char(1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "gen_char failed");
        post_event(FINGER_EVT_IMAGE_FAIL, NULL, 0);
        wait_finger_remove();
        return;
    }
    
    ESP_LOGI(TAG, "Image processed, searching...");
    post_event(FINGER_EVT_IMAGE_OK, NULL, 0);

    int match_id = -1;
    uint16_t score = 0;
    ret = as608_search(&match_id, &score);
    
    if (ret == ESP_OK && match_id >= 0) {
        finger_match_data_t data = { .finger_id = (int16_t)match_id, .score = score };
        ESP_LOGI(TAG, ">>> MATCH: ID=%d Score=%d <<<", match_id, score);
        post_event(FINGER_EVT_MATCH, &data, sizeof(data));
    } else {
        ESP_LOGI(TAG, "No match found");
        post_event(FINGER_EVT_NO_MATCH, NULL, 0);
    }

    wait_finger_remove();
}

static void do_enroll_step1(void) {
    esp_err_t ret = as608_read_image();
    
    if (ret == ESP_ERR_NOT_FOUND) {
        if (s_finger_on) {
            s_finger_on = false;
            post_event(FINGER_EVT_REMOVED, NULL, 0);
        }
        return;
    }
    
    if (ret != ESP_OK) {
        return;
    }

    if (!s_finger_on) {
        s_finger_on = true;
        ESP_LOGI(TAG, "Enroll: Finger detected - Keep finger STILL!");
        post_event(FINGER_EVT_DETECTED, NULL, 0);
    }

    /* Small delay for stable capture */
    vTaskDelay(pdMS_TO_TICKS(200));

    ret = as608_gen_char(1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Enroll step1: gen_char failed");
        post_event(FINGER_EVT_IMAGE_FAIL, NULL, 0);
        wait_finger_remove();
        return;
    }

    ESP_LOGI(TAG, "Enroll step1 OK - Features extracted to buffer1");
    ESP_LOGI(TAG, ">>> REMOVE FINGER, then place SAME finger again <<<");
    finger_enroll_data_t data = { .finger_id = s_enroll_id, .step = 1 };
    post_event(FINGER_EVT_ENROLL_STEP1, &data, sizeof(data));

    wait_finger_remove();
    post_event(FINGER_EVT_REMOVED, NULL, 0);
    
    /* Wait a moment before accepting second scan */
    vTaskDelay(pdMS_TO_TICKS(500));
    
    s_state = STATE_ENROLL_STEP2;
}

static void do_enroll_step2(void) {
    esp_err_t ret = as608_read_image();
    
    if (ret == ESP_ERR_NOT_FOUND) {
        return;
    }
    
    if (ret != ESP_OK) {
        return;
    }

    s_finger_on = true;
    ESP_LOGI(TAG, "Enroll: Second capture - Keep finger STILL!");
    post_event(FINGER_EVT_DETECTED, NULL, 0);

    /* Small delay for stable capture */
    vTaskDelay(pdMS_TO_TICKS(200));

    ret = as608_gen_char(2);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Enroll step2: gen_char failed");
        post_event(FINGER_EVT_IMAGE_FAIL, NULL, 0);
        wait_finger_remove();
        s_state = STATE_ENROLL_STEP1;  /* Retry from step1 */
        return;
    }

    ESP_LOGI(TAG, "Enroll step2 OK - Features extracted to buffer2");
    finger_enroll_data_t data = { .finger_id = s_enroll_id, .step = 2 };
    post_event(FINGER_EVT_ENROLL_STEP2, &data, sizeof(data));

    s_state = STATE_ENROLL_STORE;
}

static void do_enroll_store(void) {
    /* Final validation before store */
    if (s_enroll_id < 0 || s_enroll_id >= CFG_AS608_LIBRARY_SIZE) {
        ESP_LOGE(TAG, "FATAL: Invalid enroll_id=%d before store! (valid: 0-%d)",
                 s_enroll_id, CFG_AS608_LIBRARY_SIZE - 1);
        post_event(FINGER_EVT_ENROLL_FAIL, NULL, 0);
        goto cleanup;
    }
    
    ESP_LOGI(TAG, "Creating template from buffer1 + buffer2...");
    
    esp_err_t ret = as608_reg_model();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "reg_model failed - fingerprints may not match!");
        ESP_LOGE(TAG, "Make sure to place the SAME finger in the SAME position");
        post_event(FINGER_EVT_ENROLL_FAIL, NULL, 0);
        goto cleanup;
    }
    
    ESP_LOGI(TAG, "Template created OK, storing to ID %d...", s_enroll_id);

    ret = as608_store(s_enroll_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "store failed for ID %d", s_enroll_id);
        post_event(FINGER_EVT_ENROLL_FAIL, NULL, 0);
        goto cleanup;
    }

    ESP_LOGI(TAG, ">>> ENROLL SUCCESS: ID=%d <<<", s_enroll_id);
    finger_enroll_data_t data = { .finger_id = s_enroll_id, .step = 0 };
    post_event(FINGER_EVT_ENROLL_OK, &data, sizeof(data));
    
    /* Update template count */
    as608_get_template_count(&s_template_count);
    ESP_LOGI(TAG, "Template count now: %d", s_template_count);

cleanup:
    wait_finger_remove();
    s_state = STATE_SEARCHING;
    s_enroll_id = -1;
    s_finger_on = false;
}

static void finger_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "Finger task started - scanning...");
    s_state = STATE_SEARCHING;
    cmd_msg_t msg;

    while (1) {
        /* Check for commands */
        if (xQueueReceive(s_queue, &msg, 0) == pdTRUE) {
            process_cmd(&msg);
        }

        switch (s_state) {
            case STATE_IDLE:
                vTaskDelay(pdMS_TO_TICKS(CFG_FINGER_SCAN_INTERVAL_MS));
                break;
                
            case STATE_SEARCHING:
                do_search();
                vTaskDelay(pdMS_TO_TICKS(CFG_FINGER_SCAN_FAST_MS));
                break;
                
            case STATE_ENROLL_STEP1:
                do_enroll_step1();
                vTaskDelay(pdMS_TO_TICKS(CFG_FINGER_SCAN_FAST_MS));
                break;
                
            case STATE_ENROLL_STEP2:
                do_enroll_step2();
                vTaskDelay(pdMS_TO_TICKS(CFG_FINGER_SCAN_FAST_MS));
                break;
                
            case STATE_ENROLL_STORE:
                do_enroll_store();
                break;
        }
    }
}

esp_err_t finger_init(void) {
    if (s_task != NULL) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    as608_config_t cfg = {
        .uart_num = CFG_AS608_UART_PORT,
        .tx_pin = CFG_AS608_TX_GPIO,
        .rx_pin = CFG_AS608_RX_GPIO,
        .baudrate = CFG_AS608_BAUD_RATE,
    };

    esp_err_t ret = as608_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "as608_init failed: %s", esp_err_to_name(ret));
        post_event(FINGER_EVT_ERROR, NULL, 0);
        return ret;
    }

    /* Get initial template count */
    as608_get_template_count(&s_template_count);
    ESP_LOGI(TAG, "Templates in database: %d", s_template_count);

    post_event(FINGER_EVT_READY, NULL, 0);

    /* Create queue */
    s_queue = xQueueCreate(CFG_FINGER_QUEUE_SIZE, sizeof(cmd_msg_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "Queue create failed");
        return ESP_ERR_NO_MEM;
    }

    /* Create task */
    if (xTaskCreate(finger_task, "finger", CFG_FINGER_TASK_STACK, NULL,
                    CFG_FINGER_TASK_PRIORITY, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "Task create failed");
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Initialized successfully");
    return ESP_OK;
}

void finger_deinit(void) {
    if (s_task) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
    if (s_queue) {
        vQueueDelete(s_queue);
        s_queue = NULL;
    }
    as608_deinit();
    ESP_LOGI(TAG, "Deinitialized");
}

static esp_err_t send_cmd(finger_cmd_t cmd, int16_t param) {
    if (s_queue == NULL) return ESP_ERR_INVALID_STATE;
    cmd_msg_t msg = { .cmd = cmd, .param = param };
    if (xQueueSend(s_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t finger_start_search(void) {
    return send_cmd(FINGER_CMD_SEARCH, 0);
}

esp_err_t finger_start_enroll(int16_t finger_id) {
    return send_cmd(FINGER_CMD_ENROLL, finger_id);
}

esp_err_t finger_cancel(void) {
    return send_cmd(FINGER_CMD_CANCEL, 0);
}

esp_err_t finger_delete(int16_t finger_id) {
    esp_err_t ret = as608_delete(finger_id);
    if (ret == ESP_OK) {
        as608_get_template_count(&s_template_count);
    }
    post_event(ret == ESP_OK ? FINGER_EVT_DELETE_OK : FINGER_EVT_DELETE_FAIL, NULL, 0);
    return ret;
}

esp_err_t finger_delete_all(void) {
    esp_err_t ret = as608_empty();
    if (ret == ESP_OK) {
        s_template_count = 0;
    }
    post_event(ret == ESP_OK ? FINGER_EVT_DELETE_ALL_OK : FINGER_EVT_DELETE_FAIL, NULL, 0);
    return ret;
}

esp_err_t finger_search_once(void) {
    return send_cmd(FINGER_CMD_SEARCH, 0);
}

uint16_t finger_get_template_count(void) {
    return s_template_count;
}

uint16_t finger_get_library_size(void) {
    return CFG_AS608_LIBRARY_SIZE;
}

bool finger_is_connected(void) {
    return s_task != NULL;
}

bool finger_is_id_used(int id) {
    /* Simple check: ID is used if it's less than current template count
     * This assumes sequential storage without gaps.
     * For accurate check, would need to read AS608 index table.
     */
    return (id >= 0 && id < (int)s_template_count);
}

bool finger_is_id_valid(int id) {
    return (id >= 0 && id < CFG_AS608_LIBRARY_SIZE);
}
