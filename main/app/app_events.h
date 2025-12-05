/**
 * @file app_events.h
 * @brief Application Event Definitions
 */

#ifndef APP_EVENTS_H
#define APP_EVENTS_H

#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Event Base
 * ============================================================================ */
ESP_EVENT_DECLARE_BASE(APP_EVENT);

/* ============================================================================
 * Event IDs
 * ============================================================================ */
typedef enum {
    APP_EVT_STARTED = 0,
    APP_EVT_READY,
    APP_EVT_ERROR,
    APP_EVT_MODE_IDLE,
    APP_EVT_MODE_ENROLL,
    APP_EVT_MODE_SEARCH,
    APP_EVT_MODE_DELETE,
    APP_EVT_MODE_CONFIG_WIFI,
    APP_EVT_FINGER_MATCHED,
    APP_EVT_FINGER_NOT_MATCHED,
} app_event_id_t;

/* ============================================================================
 * Event Data
 * ============================================================================ */
typedef struct {
    esp_err_t    code;
    const char  *message;
} app_error_data_t;

typedef struct {
    int16_t  finger_id;
    uint16_t score;
} app_finger_match_data_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_EVENTS_H */
