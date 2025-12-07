#ifndef AS608_DRIVER_H
#define AS608_DRIVER_H

#include <stdint.h>
#include <esp_event.h>

#ifdef __cplusplus
extern "C" {
#endif

// Event base
ESP_EVENT_DECLARE_BASE(AS608_EVENT);

// Event IDs
typedef enum {
    AS608_EVENT_FINGERPRINT_DETECTED,
    AS608_EVENT_MATCH_FOUND,
    AS608_EVENT_MATCH_NOT_FOUND,
    AS608_EVENT_ENROLL_SUCCESS,
    AS608_EVENT_ENROLL_FAIL,
    AS608_EVENT_DELETE_SUCCESS,
    AS608_EVENT_DELETE_FAIL,
} as608_event_id_t;

// Configuration structure
typedef struct {
    int uart_num;
    int baud_rate;
    int tx_pin;
    int rx_pin;
    int buffer_size;
} as608_config_t;

// ID storage structure
typedef struct {
    uint16_t id;
    char name[32];
} as608_id_t;

// Initialize the AS608 driver
esp_err_t as608_driver_init(const as608_config_t *config);

// Deinitialize the driver
esp_err_t as608_driver_deinit();

// Enroll a fingerprint
esp_err_t as608_enroll_fingerprint(uint16_t id, const char *name);

// Verify a fingerprint
esp_err_t as608_verify_fingerprint();

// Delete a fingerprint by ID
esp_err_t as608_delete_fingerprint(uint16_t id);

// Get list of stored IDs
esp_err_t as608_get_stored_ids(as608_id_t *ids, size_t *count, size_t max_count);

// Check if ID exists
bool as608_id_exists(uint16_t id);

#ifdef __cplusplus
}
#endif

#endif // AS608_DRIVER_H