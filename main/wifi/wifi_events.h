/**
 * @file wifi_events.h
 * @brief WiFi Module Event Definitions
 */

#ifndef WIFI_EVENTS_H
#define WIFI_EVENTS_H

#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(WIFI_MODULE_EVENT);

typedef enum {
    WIFI_EVT_CONNECTING = 0,
    WIFI_EVT_CONNECTED,
    WIFI_EVT_DISCONNECTED,
    WIFI_EVT_SCAN_DONE,
    WIFI_EVT_AP_STARTED,
    WIFI_EVT_AP_STOPPED,
    WIFI_EVT_CONFIG_SAVED,
} wifi_module_event_id_t;

typedef struct {
    char ssid[33];
    char ip[16];
    int8_t rssi;
} wifi_connected_data_t;

#ifdef __cplusplus
}
#endif

#endif /* WIFI_EVENTS_H */
