/**
 * @file types.h
 * @brief Common Type Definitions
 */

#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Application States
 * ============================================================================ */
typedef enum {
    APP_STATE_INIT = 0,
    APP_STATE_IDLE,
    APP_STATE_SEARCHING,
    APP_STATE_ENROLL_STEP1,
    APP_STATE_ENROLL_STEP2,
    APP_STATE_ENROLL_STORE,
    APP_STATE_DELETING,
    APP_STATE_CONFIG_WIFI,
    APP_STATE_ERROR,
    APP_STATE_MAX
} app_state_t;

/* ============================================================================
 * Common Result Structure
 * ============================================================================ */
typedef struct {
    esp_err_t   code;
    const char *message;
} result_t;

#ifdef __cplusplus
}
#endif

#endif /* COMMON_TYPES_H */
