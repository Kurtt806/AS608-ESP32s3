/**
 * @file webserver.h
 * @brief Web server module for AS608 control panel
 *
 * Provides HTTP server with WebSocket for realtime device control.
 * Uses event-based WebSocket communication for real-time updates.
 */

#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Event Types for WebSocket
 * ============================================================================ */
typedef enum {
    WS_EVENT_IDLE = 0,
    WS_EVENT_FINGER_DETECTED,
    WS_EVENT_ENROLL_STEP1_OK,
    WS_EVENT_ENROLL_STEP2_OK,
    WS_EVENT_REMOVE_FINGER,
    WS_EVENT_SAVING,
    WS_EVENT_STORE_OK,
    WS_EVENT_STORE_FAIL,
    WS_EVENT_MATCH,
    WS_EVENT_NO_MATCH,
    WS_EVENT_DELETE_OK,
    WS_EVENT_CLEAR_OK,
    WS_EVENT_ERROR,
    WS_EVENT_ENROLLING,
    WS_EVENT_SEARCHING,
} ws_event_type_t;

/* ============================================================================
 * Initialization & Control
 * ============================================================================ */

/**
 * @brief Initialize the web server module
 * @return true on success
 */
bool webserver_init(void);

/**
 * @brief Start the web server
 * @return true on success
 */
bool webserver_start(void);

/**
 * @brief Stop the web server
 */
void webserver_stop(void);

/**
 * @brief Check if web server is running
 * @return true if running
 */
bool webserver_is_running(void);

/* ============================================================================
 * WebSocket Event Broadcasting
 * ============================================================================ */

/**
 * @brief Send event to all WebSocket clients
 * @param event Event name string
 * @param value Optional value (use -1 if not applicable)
 * 
 * Example usage:
 *   finger_send_event("finger_detected", 0);
 *   finger_send_event("enroll_step1_ok", 0);
 *   finger_send_event("match", matched_id);
 */
void finger_send_event(const char *event, int value);

/**
 * @brief Broadcast state change to all clients
 * @param state New state string (idle, enrolling, searching, etc.)
 */
void webserver_broadcast_state(const char *state);

/**
 * @brief Broadcast fingerprint match result
 * @param id Matched fingerprint ID (-1 for no match)
 * @param score Match score
 */
void webserver_broadcast_match(int id, int score);

/**
 * @brief Broadcast enrollment step progress
 * @param step Current step (1 or 2)
 */
void webserver_broadcast_enroll_step(int step);

/**
 * @brief Broadcast enrollment complete
 * @param id New fingerprint ID
 */
void webserver_broadcast_enroll_ok(int id);

/**
 * @brief Broadcast delete result
 * @param id Deleted fingerprint ID (-1 for all)
 */
void webserver_broadcast_delete(int id);

/**
 * @brief Broadcast error event
 * @param message Error message
 */
void webserver_broadcast_error(const char *message);

/* ============================================================================
 * Legacy API (for backward compatibility)
 * ============================================================================ */

/**
 * @brief Broadcast event with optional message
 * @param event Event name
 * @param message Optional message
 */
void webserver_broadcast_event(const char *event, const char *message);

#ifdef __cplusplus
}
#endif

#endif /* WEBSERVER_H */
