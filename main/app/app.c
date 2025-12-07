/**
 * @file app.c
 * @brief Application Controller Implementation
 */

#include "app.h"
#include "app_events.h"
#include "../common/config.h"
#include "../settings/settings.h"
#include "../finger/finger.h"
#include "../finger/finger_events.h"
#include "../button/button.h"
#include "../button/button_events.h"
#include "audio.h"
#include "../wifi/wifi.h"
#include "../webserver/webserver.h"
#include "../finger/finger_meta.h"

#include <stdbool.h>
#include "esp_log.h"
#include "esp_event.h"

static const char *TAG = "APP";

/* ============================================================================
 * Event Base Definition
 * ============================================================================ */
ESP_EVENT_DEFINE_BASE(APP_EVENT);

/* ============================================================================
 * State
 * ============================================================================ */
static app_state_t s_state = APP_STATE_INIT;
static int16_t s_enroll_id = -1;
static int16_t s_delete_id = -1;  /* ID being deleted */
static esp_event_handler_instance_t s_finger_handler = NULL;
static esp_event_handler_instance_t s_button_handler = NULL;

/* ============================================================================
 * Helpers
 * ============================================================================ */
static const char *state_str(app_state_t s)
{
    static const char *names[] = {
        [APP_STATE_INIT]         = "INIT",
        [APP_STATE_IDLE]         = "IDLE",
        [APP_STATE_SEARCHING]    = "SEARCHING",
        [APP_STATE_ENROLL_STEP1] = "ENROLL_STEP1",
        [APP_STATE_ENROLL_STEP2] = "ENROLL_STEP2",
        [APP_STATE_ENROLL_STORE] = "ENROLL_STORE",
        [APP_STATE_DELETING]     = "DELETING",
        [APP_STATE_CONFIG_WIFI]  = "CONFIG_WIFI",
        [APP_STATE_ERROR]        = "ERROR",
    };
    return (s < APP_STATE_MAX) ? names[s] : "UNKNOWN";
}

static void set_state(app_state_t new_state)
{
    if (s_state == new_state)
        return;
    ESP_LOGI(TAG, "State: %s -> %s", state_str(s_state), state_str(new_state));
    s_state = new_state;

    app_event_id_t evt = APP_EVT_MODE_IDLE;
    switch (new_state)
    {
    case APP_STATE_IDLE:
        evt = APP_EVT_MODE_IDLE;
        break;
    case APP_STATE_SEARCHING:
        evt = APP_EVT_MODE_SEARCH;
        break;
    case APP_STATE_ENROLL_STEP1:
    case APP_STATE_ENROLL_STEP2:
    case APP_STATE_ENROLL_STORE:
        evt = APP_EVT_MODE_ENROLL;
        break;
    case APP_STATE_DELETING:
        evt = APP_EVT_MODE_DELETE;
        break;
    default:
        return;
    }
    esp_event_post(APP_EVENT, evt, NULL, 0, 0);
}

/* ============================================================================
 * Finger Event Handler
 * ============================================================================ */
static void on_finger_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;

    switch ((finger_event_id_t)id)
    {
    case FINGER_EVT_READY:
        ESP_LOGI(TAG, "[FINGER] Ready");
        audio_play(SOUND_READY);
        finger_send_event("idle", -1);
        break;

    case FINGER_EVT_ERROR:
        ESP_LOGE(TAG, "[FINGER] Error");
        audio_play(SOUND_ERROR);
        set_state(APP_STATE_ERROR);
        webserver_broadcast_error("Sensor error");
        break;

    case FINGER_EVT_DETECTED:
        ESP_LOGI(TAG, "[FINGER] Detected");
        audio_play(SOUND_FINGER_DETECTED);
        finger_send_event("finger_detected", -1);
        break;

    case FINGER_EVT_REMOVED:
        ESP_LOGI(TAG, "[FINGER] Removed");
        finger_send_event("remove_finger", -1);
        break;

    case FINGER_EVT_MATCH:
    {
        finger_match_data_t *m = (finger_match_data_t *)data;
        ESP_LOGI(TAG, "[FINGER] Match id=%d score=%u", m->finger_id, m->score);
        audio_play(SOUND_MATCH_OK);
        
        /* Record match statistics */
        finger_meta_record_match(m->finger_id);
        
        webserver_broadcast_match(m->finger_id, m->score);
        set_state(APP_STATE_IDLE);
        break;
    }

    case FINGER_EVT_NO_MATCH:
        ESP_LOGI(TAG, "[FINGER] No match");
        audio_play(SOUND_MATCH_FAIL);
        webserver_broadcast_match(-1, 0);
        set_state(APP_STATE_IDLE);
        break;

    case FINGER_EVT_ENROLL_START:
    {
        finger_enroll_data_t *e = (finger_enroll_data_t *)data;
        s_enroll_id = e->finger_id;
        ESP_LOGI(TAG, "[FINGER] Enroll start id=%d", s_enroll_id);
        audio_play(SOUND_ENROLL_START);
        finger_send_event("enrolling", -1);
        break;
    }

    case FINGER_EVT_ENROLL_STEP1:
        ESP_LOGI(TAG, "[FINGER] Enroll step1 OK - remove finger");
        audio_play(SOUND_ENROLL_STEP);
        webserver_broadcast_enroll_step(1);
        finger_send_event("remove_finger", -1);
        set_state(APP_STATE_ENROLL_STEP2);
        break;

    case FINGER_EVT_ENROLL_STEP2:
        ESP_LOGI(TAG, "[FINGER] Enroll step2 OK");
        audio_play(SOUND_ENROLL_STEP);
        webserver_broadcast_enroll_step(2);
        finger_send_event("saving", -1);
        set_state(APP_STATE_ENROLL_STORE);
        break;

    case FINGER_EVT_ENROLL_OK:
    {
        finger_enroll_data_t *e = (finger_enroll_data_t *)data;
        int enrolled_id = e ? e->finger_id : s_enroll_id;
        ESP_LOGI(TAG, "[FINGER] Enroll success id=%d", enrolled_id);
        audio_play(SOUND_ENROLL_OK);
        
        /* Create metadata entry for new fingerprint (auto-generated name) */
        finger_meta_create(enrolled_id, NULL);
        
        webserver_broadcast_enroll_ok(enrolled_id);
        s_enroll_id = -1;
        set_state(APP_STATE_IDLE);
        break;
    }

    case FINGER_EVT_ENROLL_FAIL:
        ESP_LOGE(TAG, "[FINGER] Enroll fail");
        audio_play(SOUND_ENROLL_FAIL);
        finger_send_event("store_fail", -1);
        s_enroll_id = -1;
        set_state(APP_STATE_IDLE);
        break;

    case FINGER_EVT_ENROLL_CANCEL:
        ESP_LOGI(TAG, "[FINGER] Enroll cancelled");
        audio_play(SOUND_BEEP);
        finger_send_event("idle", -1);
        s_enroll_id = -1;
        set_state(APP_STATE_IDLE);
        break;

    case FINGER_EVT_DELETE_OK:
        ESP_LOGI(TAG, "[FINGER] Delete OK id=%d", s_delete_id);
        audio_play(SOUND_DELETE_OK);
        
        /* Delete metadata for this fingerprint */
        if (s_delete_id >= 0) {
            finger_meta_delete_name(s_delete_id);
        }
        
        webserver_broadcast_delete(s_delete_id);
        s_delete_id = -1;
        set_state(APP_STATE_IDLE);
        break;

    case FINGER_EVT_DELETE_FAIL:
        ESP_LOGE(TAG, "[FINGER] Delete fail id=%d", s_delete_id);
        audio_play(SOUND_ERROR);
        webserver_broadcast_error("Delete failed");
        s_delete_id = -1;
        set_state(APP_STATE_IDLE);
        break;

    case FINGER_EVT_DELETE_ALL_OK:
        ESP_LOGI(TAG, "[FINGER] Delete all OK");
        audio_play(SOUND_DELETE_OK);
        
        /* Clear all metadata */
        finger_meta_clear_all();
        
        webserver_broadcast_delete(-1);
        s_delete_id = -1;
        set_state(APP_STATE_IDLE);
        break;

    default:
        break;
    }
}

/* ============================================================================
 * Button Event Handler
 * ============================================================================ */
static void on_button_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    btn_event_data_t *btn = (btn_event_data_t *)data;

    switch ((button_event_id_t)id)
    {
    case BUTTON_EVT_CLICK:
        ESP_LOGI(TAG, "[BUTTON] Click btn=%d", btn->btn_id);
        if (btn->btn_id == BTN_ID_BOOT)
        {
            /* Single click: toggle enroll/cancel */
            if (s_state == APP_STATE_IDLE)
            {
                app_start_enroll();
            }
            else if (s_state == APP_STATE_ENROLL_STEP1 ||
                     s_state == APP_STATE_ENROLL_STEP2 ||
                     s_state == APP_STATE_ENROLL_STORE)
            {
                app_cancel();
            }
        }
        break;

    case BUTTON_EVT_DOUBLE_CLICK:
        ESP_LOGI(TAG, "[BUTTON] Double click btn=%d", btn->btn_id);
        if (btn->btn_id == BTN_ID_BOOT)
        {
            /* Double click: cancel current operation */
            // app_cancel();
            // Start WiFi configuration
            app_start_wifi_config();
        }
        break;

    case BUTTON_EVT_LONG_PRESS:
        ESP_LOGI(TAG, "[BUTTON] Long press btn=%d", btn->btn_id);
        if (btn->btn_id == BTN_ID_BOOT)
        {
            /* Long press: delete all fingerprints */
            app_delete_finger(-1);
        }
        break;

    default:
        break;
    }
}

/* ============================================================================
 * Public API
 * ============================================================================ */
esp_err_t app_init(void)
{
    ESP_LOGI(TAG, "Initializing...");

    /* Register for finger events */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        FINGER_EVENT, ESP_EVENT_ANY_ID,
        on_finger_event, NULL, &s_finger_handler));

    /* Register for button events */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        BUTTON_EVENT, ESP_EVENT_ANY_ID,
        on_button_event, NULL, &s_button_handler));

    /* Init button module */
    esp_err_t ret = button_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "button_init fail: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Init audio module */
    ret = audio_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "audio_init fail: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Init WiFi module */
    ret = wifi_module_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "wifi_module_init fail: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Init finger module */
    ret = finger_init();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "finger_init fail: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

void app_start(void)
{
    ESP_LOGI(TAG, "Starting...");

    /* Start WiFi */
    wifi_module_start();

    set_state(APP_STATE_IDLE);
    esp_event_post(APP_EVENT, APP_EVT_STARTED, NULL, 0, portMAX_DELAY);
    ESP_LOGI(TAG, "Started");
}

void app_stop(void)
{
    ESP_LOGI(TAG, "Stopping...");

    /* Unregister event handlers */
    if (s_finger_handler)
    {
        esp_event_handler_instance_unregister(FINGER_EVENT, ESP_EVENT_ANY_ID, s_finger_handler);
        s_finger_handler = NULL;
    }
    if (s_button_handler)
    {
        esp_event_handler_instance_unregister(BUTTON_EVENT, ESP_EVENT_ANY_ID, s_button_handler);
        s_button_handler = NULL;
    }

    /* Deinit modules in reverse order */
    wifi_module_stop();
    finger_deinit();
    audio_deinit();
    button_deinit();
    
    ESP_LOGI(TAG, "Stopped");
}

app_state_t app_get_state(void)
{
    return s_state;
}

int16_t app_start_enroll(void)
{
    if (s_state != APP_STATE_IDLE)
    {
        ESP_LOGW(TAG, "Cannot enroll: not IDLE");
        return -1;
    }

    ESP_LOGI(TAG, "Request enroll (auto find slot)");
    set_state(APP_STATE_ENROLL_STEP1);

    /* Send -1 to let finger task find slot */
    finger_start_enroll(-1);
    return 0; /* Will get actual ID via event */
}

void app_cancel(void)
{
    ESP_LOGI(TAG, "Cancel");
    if (s_state == APP_STATE_ENROLL_STEP1 ||
        s_state == APP_STATE_ENROLL_STEP2 ||
        s_state == APP_STATE_ENROLL_STORE)
    {
        finger_cancel();
    }
    s_enroll_id = -1;
    set_state(APP_STATE_IDLE);
}

esp_err_t app_delete_finger(int16_t finger_id)
{
    if (s_state != APP_STATE_IDLE)
    {
        ESP_LOGW(TAG, "Cannot delete: not IDLE");
        return ESP_ERR_INVALID_STATE;
    }

        s_delete_id = finger_id;  /* Save ID being deleted */
    set_state(APP_STATE_DELETING);
    if (finger_id < 0)
    {
        return finger_delete_all();
    }
    return finger_delete(finger_id);
}

void app_start_wifi_config(void)
{
    if (s_state != APP_STATE_IDLE)
    {
        ESP_LOGW(TAG, "Cannot start WiFi config: not IDLE");
        return;
    }

    ESP_LOGI(TAG, "Starting WiFi configuration...");
    set_state(APP_STATE_CONFIG_WIFI);
    wifi_module_stop();
    wifi_module_start_config_ap();
    esp_event_post(APP_EVENT, APP_EVT_MODE_CONFIG_WIFI, NULL, 0, 0);
}

void app_stop_wifi_config(void)
{
    if (s_state != APP_STATE_CONFIG_WIFI)
    {
        return;
    }

    ESP_LOGI(TAG, "Stopping WiFi configuration...");
    wifi_module_stop_config_ap();
    wifi_module_start();
    set_state(APP_STATE_IDLE);
}

/* ============================================================================
 * Web Interface API
 * ============================================================================ */
static bool s_auto_search = true;

const char* app_get_state_string(void)
{
    return state_str(s_state);
}

void app_request_enroll(void)
{
    if (s_state == APP_STATE_IDLE)
    {
        app_start_enroll();
    }
}

void app_request_search(void)
{
    if (s_state == APP_STATE_IDLE)
    {
        set_state(APP_STATE_SEARCHING);
        finger_search_once();
    }
}

void app_request_cancel(void)
{
    app_cancel();
}

void app_request_delete(int id)
{
    if (id >= 0)
    {
        app_delete_finger((int16_t)id);
    }
}

void app_request_delete_all(void)
{
    app_delete_finger(-1);
}

void app_set_auto_search(bool enabled)
{
    s_auto_search = enabled;
    ESP_LOGI(TAG, "Auto search: %s", enabled ? "enabled" : "disabled");
    /* TODO: Actually use this to control finger module behavior */
}

bool app_get_auto_search(void)
{
    return s_auto_search;
}
