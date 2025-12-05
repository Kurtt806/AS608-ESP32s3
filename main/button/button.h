/**
 * @file button.h
 * @brief Button Module API
 */

#ifndef BUTTON_H
#define BUTTON_H

#include "esp_err.h"
#include "driver/gpio.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Button IDs
 * ============================================================================ */
typedef enum {
    BTN_ID_BOOT = 0,
    BTN_ID_MAX
} button_id_t;

/* ============================================================================
 * API
 * ============================================================================ */

/**
 * @brief Initialize button module
 * @return ESP_OK on success
 */
esp_err_t button_init(void);

/**
 * @brief Deinitialize button module
 */
void button_deinit(void);

/**
 * @brief Check if button is pressed
 * @param id Button ID
 * @return true if pressed
 */
bool button_is_pressed(button_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_H */
