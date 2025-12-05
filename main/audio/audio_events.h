/**
 * @file audio_events.h
 * @brief Audio Module Event Definitions
 */

#ifndef AUDIO_EVENTS_H
#define AUDIO_EVENTS_H

#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Event Base
 * ============================================================================ */
ESP_EVENT_DECLARE_BASE(AUDIO_EVENT);

/* ============================================================================
 * Event IDs
 * ============================================================================ */
typedef enum {
    AUDIO_EVT_PLAY_START = 0,
    AUDIO_EVT_PLAY_DONE,
    AUDIO_EVT_ERROR,
} audio_event_id_t;

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_EVENTS_H */
