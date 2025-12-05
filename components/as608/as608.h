#ifndef AS608_H
#define AS608_H

#include <esp_err.h>
#include <stdint.h>
#include "as608_protocol.h"

typedef struct {
    int uart_num;
    int tx_pin;
    int rx_pin;
    int baudrate;
} as608_config_t;

/**
 * @brief Initialize AS608 sensor
 */
esp_err_t as608_init(const as608_config_t *cfg);

/**
 * @brief Deinitialize AS608 sensor
 */
void as608_deinit(void);

/**
 * @brief Read fingerprint image from sensor
 * @return ESP_OK if finger detected and image captured
 *         ESP_ERR_NOT_FOUND if no finger
 *         ESP_FAIL on error
 */
esp_err_t as608_read_image(void);

/**
 * @brief Generate character file from image
 * @param buffer_id 1 or 2
 */
esp_err_t as608_gen_char(int buffer_id);

/**
 * @brief Combine two character buffers into template
 */
esp_err_t as608_reg_model(void);

/**
 * @brief Store template to flash
 * @param id Location ID (0-based)
 */
esp_err_t as608_store(int id);

/**
 * @brief Search fingerprint in library
 * @param match_id Output matched ID (-1 if not found)
 * @param score Output match score
 * @return ESP_OK if found, ESP_ERR_NOT_FOUND if not matched
 */
esp_err_t as608_search(int *match_id, uint16_t *score);

/**
 * @brief Delete template from flash
 */
esp_err_t as608_delete(int id);

/**
 * @brief Delete all templates
 */
esp_err_t as608_empty(void);

/**
 * @brief Get template count
 */
esp_err_t as608_get_template_count(uint16_t *count);

/**
 * @brief Check if sensor is connected (handshake)
 */
esp_err_t as608_handshake(void);

#endif // AS608_H
