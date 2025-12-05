/**
 * @file button_events.h
 * @brief Button Module Event Definitions
 */

#ifndef BUTTON_EVENTS_H
#define BUTTON_EVENTS_H

#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Event Base
 * ============================================================================ */
ESP_EVENT_DECLARE_BASE(BUTTON_EVENT);

/* ============================================================================
 * Event IDs
 * ============================================================================ */
typedef enum {
    BUTTON_EVT_CLICK = 0,
    BUTTON_EVT_DOUBLE_CLICK,
    BUTTON_EVT_LONG_PRESS,
    BUTTON_EVT_PRESS_DOWN,
    BUTTON_EVT_PRESS_UP,
} button_event_id_t;

/* ============================================================================
 * Event Data
 * ============================================================================ */
typedef struct {
    uint8_t btn_id;         /**< Which button triggered */
} btn_event_data_t;

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_EVENTS_H */
