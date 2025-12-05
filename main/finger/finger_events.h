/**
 * @file finger_events.h
 * @brief Fingerprint Module Event Definitions
 */

#ifndef FINGER_EVENTS_H
#define FINGER_EVENTS_H

#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Event Base
 * ============================================================================ */
ESP_EVENT_DECLARE_BASE(FINGER_EVENT);

/* ============================================================================
 * Event IDs
 * ============================================================================ */
typedef enum {
    /* Sensor Status */
    FINGER_EVT_READY = 0,
    FINGER_EVT_ERROR,

    /* Detection */
    FINGER_EVT_DETECTED,
    FINGER_EVT_REMOVED,
    FINGER_EVT_IMAGE_OK,
    FINGER_EVT_IMAGE_FAIL,

    /* Search */
    FINGER_EVT_MATCH,
    FINGER_EVT_NO_MATCH,

    /* Enrollment */
    FINGER_EVT_ENROLL_START,
    FINGER_EVT_ENROLL_STEP1,
    FINGER_EVT_ENROLL_STEP2,
    FINGER_EVT_ENROLL_OK,
    FINGER_EVT_ENROLL_FAIL,
    FINGER_EVT_ENROLL_CANCEL,

    /* Delete */
    FINGER_EVT_DELETE_OK,
    FINGER_EVT_DELETE_FAIL,
    FINGER_EVT_DELETE_ALL_OK,
} finger_event_id_t;

/* ============================================================================
 * Event Data Structures
 * ============================================================================ */
typedef struct {
    int16_t  finger_id;
    uint16_t score;
} finger_match_data_t;

typedef struct {
    int16_t finger_id;
    uint8_t step;
} finger_enroll_data_t;

#ifdef __cplusplus
}
#endif

#endif /* FINGER_EVENTS_H */
