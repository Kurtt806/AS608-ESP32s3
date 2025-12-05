/**
 * @file webserver.h
 * @brief Web server module for AS608 control panel
 *
 * Provides HTTP server with WebSocket for realtime device control
 */

#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

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

/**
 * @brief Broadcast event to all WebSocket clients
 * @param event Event name
 * @param message Optional message
 */
void webserver_broadcast_event(const char *event, const char *message);

/**
 * @brief Broadcast state change to all clients
 * @param state New state string
 */
void webserver_broadcast_state(const char *state);

/**
 * @brief Broadcast fingerprint match result
 * @param id Matched fingerprint ID (-1 for no match)
 * @param score Match score
 */
void webserver_broadcast_match(int id, int score);

/**
 * @brief Broadcast enrollment progress
 * @param step Current step (1-3)
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

#ifdef __cplusplus
}
#endif

#endif /* WEBSERVER_H */
