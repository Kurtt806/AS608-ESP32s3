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

/* ============================================================================
 * Event Base Definition
 * ============================================================================ */
ESP_EVENT_DEFINE_BASE(FINGER_EVENT);

/* ============================================================================
 * Internal State
 * ============================================================================ */
typedef enum {
    STATE_IDLE = 0,
    STATE_SEARCHING,
    STATE_ENROLL_STEP1,
    STATE_ENROLL_STEP2,
    STATE_ENROLL_STORE,
} task_state_t;

typedef struct {
    finger_cmd_t cmd;
    int16_t      param;
} cmd_msg_t;

static TaskHandle_t  s_task       = NULL;
static QueueHandle_t s_queue      = NULL;
static task_state_t  s_state      = STATE_IDLE;
static int16_t       s_enroll_id  = -1;
static bool          s_finger_on  = false;
static uint32_t      s_idle_cnt   = 0;

/* ============================================================================
 * Helper: Post Event
 * ============================================================================ */
static inline void post_event(finger_event_id_t id, void *data, size_t len) {
    esp_event_post(FINGER_EVENT, id, data, len, portMAX_DELAY);
}

/* ============================================================================
 * Command Processing
 * ============================================================================ */
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

        case FINGER_CMD_ENROLL: {
            int16_t target_id = msg->param;
            
            /* Auto find slot if -1 */
            if (target_id < 0) {
                uint16_t cnt = 0;
                if (as608_get_template_count(&cnt) != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to get template count");
                    post_event(FINGER_EVT_ENROLL_FAIL, NULL, 0);
                    break;
                }
                uint16_t lib_size = as608_get_library_size();
                ESP_LOGI(TAG, "Templates: %u / %u", cnt, lib_size);
                if (cnt >= lib_size) {
                    ESP_LOGE(TAG, "Library full");
                    post_event(FINGER_EVT_ENROLL_FAIL, NULL, 0);
                    break;
                }
                target_id = (int16_t)cnt;
            }
            
            ESP_LOGI(TAG, "CMD: ENROLL id=%d", target_id);
            s_state = STATE_ENROLL_STEP1;
            s_enroll_id = target_id;
            
            /* Post enroll started event */
            finger_enroll_data_t data = { .finger_id = target_id, .step = 1 };
            post_event(FINGER_EVT_ENROLL_START, &data, sizeof(data));
            break;
        }

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

/* ============================================================================
 * State Handlers
 * ============================================================================ */
static void wait_finger_remove(void) {
    int cnt = 0;
    while (as608_get_image() == ESP_OK && cnt < 50) {
        vTaskDelay(pdMS_TO_TICKS(100));
        cnt++;
    }
    s_finger_on = false;
}

static void do_search(void) {
    esp_err_t ret = as608_get_image();
    
    if (ret == ESP_ERR_NOT_FOUND) {
        if (s_finger_on) {
            s_finger_on = false;
            s_idle_cnt = 0;
            post_event(FINGER_EVT_REMOVED, NULL, 0);
        } else {
            s_idle_cnt++;
        }
        return;
    }
    if (ret != ESP_OK) return;

    s_idle_cnt = 0;
    if (!s_finger_on) {
        s_finger_on = true;
        post_event(FINGER_EVT_DETECTED, NULL, 0);
    }

    ret = as608_gen_char(1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "gen_char failed");
        post_event(FINGER_EVT_IMAGE_FAIL, NULL, 0);
        return;
    }
    post_event(FINGER_EVT_IMAGE_OK, NULL, 0);

    int16_t id = -1;
    uint16_t score = 0;
    ret = as608_search(0, as608_get_library_size(), &id, &score);

    if (ret == ESP_OK && id >= 0) {
        finger_match_data_t data = { .finger_id = id, .score = score };
        ESP_LOGI(TAG, "MATCH id=%d score=%u", id, score);
        post_event(FINGER_EVT_MATCH, &data, sizeof(data));
    } else {
        ESP_LOGI(TAG, "NO MATCH");
        post_event(FINGER_EVT_NO_MATCH, NULL, 0);
    }

    wait_finger_remove();
}

static void do_enroll_step1(void) {
    esp_err_t ret = as608_get_image();
    
    if (ret == ESP_ERR_NOT_FOUND) {
        if (s_finger_on) {
            s_finger_on = false;
            post_event(FINGER_EVT_REMOVED, NULL, 0);
        }
        return;
    }
    if (ret != ESP_OK) return;

    if (!s_finger_on) {
        s_finger_on = true;
        post_event(FINGER_EVT_DETECTED, NULL, 0);
    }

    ret = as608_gen_char(1);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "step1 gen_char fail");
        post_event(FINGER_EVT_IMAGE_FAIL, NULL, 0);
        return;
    }

    ESP_LOGI(TAG, "ENROLL step1 OK");
    finger_enroll_data_t data = { .finger_id = s_enroll_id, .step = 1 };
    post_event(FINGER_EVT_ENROLL_STEP1, &data, sizeof(data));

    wait_finger_remove();
    post_event(FINGER_EVT_REMOVED, NULL, 0);
    s_state = STATE_ENROLL_STEP2;
}

static void do_enroll_step2(void) {
    esp_err_t ret = as608_get_image();
    if (ret == ESP_ERR_NOT_FOUND) return;
    if (ret != ESP_OK) return;

    s_finger_on = true;
    post_event(FINGER_EVT_DETECTED, NULL, 0);

    ret = as608_gen_char(2);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "step2 gen_char fail");
        post_event(FINGER_EVT_IMAGE_FAIL, NULL, 0);
        return;
    }

    ESP_LOGI(TAG, "ENROLL step2 OK");
    finger_enroll_data_t data = { .finger_id = s_enroll_id, .step = 2 };
    post_event(FINGER_EVT_ENROLL_STEP2, &data, sizeof(data));

    s_state = STATE_ENROLL_STORE;
}

static void do_enroll_store(void) {
    esp_err_t ret = as608_reg_model();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "reg_model fail");
        post_event(FINGER_EVT_ENROLL_FAIL, NULL, 0);
        goto cleanup;
    }

    ret = as608_store(s_enroll_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "store fail");
        post_event(FINGER_EVT_ENROLL_FAIL, NULL, 0);
        goto cleanup;
    }

    ESP_LOGI(TAG, "ENROLL OK id=%d", s_enroll_id);
    finger_enroll_data_t data = { .finger_id = s_enroll_id, .step = 0 };
    post_event(FINGER_EVT_ENROLL_OK, &data, sizeof(data));

cleanup:
    wait_finger_remove();
    s_state = STATE_SEARCHING;
    s_enroll_id = -1;
    s_finger_on = false;
}

/* ============================================================================
 * Task
 * ============================================================================ */
static void finger_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "Task started");

    s_state = STATE_SEARCHING;
    cmd_msg_t msg;

    while (1) {
        if (xQueueReceive(s_queue, &msg, 0) == pdTRUE) {
            process_cmd(&msg);
        }

        switch (s_state) {
            case STATE_IDLE:
                vTaskDelay(pdMS_TO_TICKS(CFG_FINGER_SCAN_INTERVAL_MS));
                break;

            case STATE_SEARCHING:
                do_search();
                vTaskDelay(pdMS_TO_TICKS(s_idle_cnt >= CFG_FINGER_IDLE_THRESHOLD ? 
                    CFG_FINGER_SCAN_INTERVAL_MS : CFG_FINGER_SCAN_FAST_MS));
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

/* ============================================================================
 * Public API
 * ============================================================================ */
esp_err_t finger_init(void) {
    if (s_task != NULL) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    /* Init AS608 */
    as608_config_t cfg = {
        .uart_port     = CFG_AS608_UART_PORT,
        .tx_gpio       = CFG_AS608_TX_GPIO,
        .rx_gpio       = CFG_AS608_RX_GPIO,
        .rst_gpio      = CFG_AS608_RST_GPIO,
        .pwr_en_gpio   = CFG_AS608_PWR_EN_GPIO,
        .baud_rate     = CFG_AS608_BAUD_RATE,
        .device_address = CFG_AS608_ADDRESS,
        .password      = CFG_AS608_PASSWORD,
        .library_size  = CFG_AS608_LIBRARY_SIZE,
        .timeout_ms    = CFG_AS608_TIMEOUT_MS,
    };

    esp_err_t ret = as608_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "as608_init fail: %s", esp_err_to_name(ret));
        post_event(FINGER_EVT_ERROR, NULL, 0);
        return ret;
    }

    ret = as608_verify_password(CFG_AS608_PASSWORD);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Password verify fail");
        post_event(FINGER_EVT_ERROR, NULL, 0);
        return ret;
    }

    /* Log params */
    as608_sys_param_t params;
    if (as608_read_sys_param(&params) == ESP_OK) {
        ESP_LOGI(TAG, "Library=%d Security=%d", params.library_size, params.security_level);
    }

    uint16_t cnt = 0;
    if (as608_get_template_count(&cnt) == ESP_OK) {
        ESP_LOGI(TAG, "Templates=%d", cnt);
    }

    post_event(FINGER_EVT_READY, NULL, 0);

    /* Create queue and task */
    s_queue = xQueueCreate(CFG_FINGER_QUEUE_SIZE, sizeof(cmd_msg_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "Queue create fail");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(finger_task, "finger", CFG_FINGER_TASK_STACK, NULL, 
                    CFG_FINGER_TASK_PRIORITY, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "Task create fail");
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Initialized");
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

esp_err_t finger_get_count(uint16_t *count) {
    return as608_get_template_count(count);
}

int16_t finger_find_free_slot(void) {
    uint16_t cnt = 0;
    esp_err_t ret = as608_get_template_count(&cnt);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "get_template_count fail: %s", esp_err_to_name(ret));
        return -1;
    }
    uint16_t lib_size = as608_get_library_size();
    ESP_LOGI(TAG, "Templates: %u / %u", cnt, lib_size);
    if (cnt >= lib_size) {
        ESP_LOGW(TAG, "Library full");
        return -1;
    }
    return (int16_t)cnt;
}

esp_err_t finger_delete(int16_t finger_id) {
    esp_err_t ret = as608_delete((uint16_t)finger_id);
    post_event(ret == ESP_OK ? FINGER_EVT_DELETE_OK : FINGER_EVT_DELETE_FAIL, NULL, 0);
    return ret;
}

esp_err_t finger_delete_all(void) {
    esp_err_t ret = as608_empty();
    post_event(ret == ESP_OK ? FINGER_EVT_DELETE_ALL_OK : FINGER_EVT_DELETE_FAIL, NULL, 0);
    return ret;
}

bool finger_is_connected(void) {
    return s_task != NULL;
}

uint16_t finger_get_library_size(void) {
    return as608_get_library_size();
}

uint16_t finger_get_template_count(void) {
    uint16_t cnt = 0;
    as608_get_template_count(&cnt);
    return cnt;
}

bool finger_is_id_used(int id) {
    /* Simple check: if id < template_count, assume used */
    /* AS608 stores templates sequentially starting from 0 */
    uint16_t cnt = finger_get_template_count();
    return (id >= 0 && id < cnt);
}

esp_err_t finger_search_once(void) {
    /* Trigger a single search cycle */
    return send_cmd(FINGER_CMD_SEARCH, 0);
}
