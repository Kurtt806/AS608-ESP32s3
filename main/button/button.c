/**
 * @file button.c
 * @brief Button Module Implementation (using iot_button component)
 */

#include "button.h"
#include "button_events.h"
#include "../common/config.h"

#include "esp_log.h"
#include "iot_button.h"

static const char *TAG = "BUTTON";

/* ============================================================================
 * Event Base Definition
 * ============================================================================ */
ESP_EVENT_DEFINE_BASE(BUTTON_EVENT);

/* ============================================================================
 * Internal State
 * ============================================================================ */
static button_handle_t s_buttons[BTN_ID_MAX] = {0};

/* ============================================================================
 * Button Callbacks
 * ============================================================================ */
static void on_click(void *arg, void *usr_data) {
    (void)arg;
    button_id_t id = (button_id_t)(uintptr_t)usr_data;
    ESP_LOGI(TAG, "Button %d: CLICK", id);
    btn_event_data_t data = { .btn_id = id };
    esp_event_post(BUTTON_EVENT, BUTTON_EVT_CLICK, &data, sizeof(data), 0);
}

static void on_double_click(void *arg, void *usr_data) {
    (void)arg;
    button_id_t id = (button_id_t)(uintptr_t)usr_data;
    ESP_LOGI(TAG, "Button %d: DOUBLE_CLICK", id);
    btn_event_data_t data = { .btn_id = id };
    esp_event_post(BUTTON_EVENT, BUTTON_EVT_DOUBLE_CLICK, &data, sizeof(data), 0);
}

static void on_long_press(void *arg, void *usr_data) {
    (void)arg;
    button_id_t id = (button_id_t)(uintptr_t)usr_data;
    ESP_LOGI(TAG, "Button %d: LONG_PRESS", id);
    btn_event_data_t data = { .btn_id = id };
    esp_event_post(BUTTON_EVENT, BUTTON_EVT_LONG_PRESS, &data, sizeof(data), 0);
}

/* ============================================================================
 * Helper
 * ============================================================================ */
static esp_err_t create_button(button_id_t id, gpio_num_t gpio, bool active_low) {
    if (gpio == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "Button %d: GPIO not configured", id);
        return ESP_OK;
    }

    button_config_t cfg = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = CFG_BTN_LONG_PRESS_MS,
        .short_press_time = CFG_BTN_SHORT_PRESS_MS,
        .gpio_button_config = {
            .gpio_num = gpio,
            .active_level = active_low ? 0 : 1,
            .disable_pull = false,
        },
    };

    s_buttons[id] = iot_button_create(&cfg);
    if (s_buttons[id] == NULL) {
        ESP_LOGE(TAG, "Button %d create failed", id);
        return ESP_FAIL;
    }

    /* Register callbacks */
    iot_button_register_cb(s_buttons[id], BUTTON_SINGLE_CLICK, on_click, (void*)(uintptr_t)id);
    iot_button_register_cb(s_buttons[id], BUTTON_DOUBLE_CLICK, on_double_click, (void*)(uintptr_t)id);
    iot_button_register_cb(s_buttons[id], BUTTON_LONG_PRESS_START, on_long_press, (void*)(uintptr_t)id);

    ESP_LOGI(TAG, "Button %d: GPIO%d initialized", id, gpio);
    return ESP_OK;
}

/* ============================================================================
 * Public API
 * ============================================================================ */
esp_err_t button_init(void) {
    ESP_LOGI(TAG, "Initializing buttons...");

    /* BOOT button (active low) */
    esp_err_t ret = create_button(BTN_ID_BOOT, CFG_BTN_BOOT_GPIO, true);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "Buttons initialized");
    return ESP_OK;
}

void button_deinit(void) {
    for (int i = 0; i < BTN_ID_MAX; i++) {
        if (s_buttons[i]) {
            iot_button_delete(s_buttons[i]);
            s_buttons[i] = NULL;
        }
    }
    ESP_LOGI(TAG, "Buttons deinitialized");
}

bool button_is_pressed(button_id_t id) {
    if (id >= BTN_ID_MAX || s_buttons[id] == NULL) {
        return false;
    }
    /* Use iot_button API to get button state */
    return iot_button_get_key_level(s_buttons[id]) == 0;  /* Active low */
}
