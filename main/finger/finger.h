/**
 * @file finger.h
 * @brief Fingerprint Module API
 */

#ifndef FINGER_H
#define FINGER_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FINGER_CMD_IDLE = 0,
    FINGER_CMD_SEARCH,
    FINGER_CMD_ENROLL,
    FINGER_CMD_CANCEL,
} finger_cmd_t;

esp_err_t finger_init(void);
void finger_deinit(void);
esp_err_t finger_start_search(void);
esp_err_t finger_start_enroll(int16_t finger_id);
esp_err_t finger_cancel(void);
esp_err_t finger_delete(int16_t finger_id);
esp_err_t finger_delete_all(void);
esp_err_t finger_search_once(void);
uint16_t finger_get_template_count(void);
uint16_t finger_get_library_size(void);
bool finger_is_connected(void);
bool finger_is_id_used(int id);
bool finger_is_id_valid(int id);

#ifdef __cplusplus
}
#endif

#endif /* FINGER_H */
