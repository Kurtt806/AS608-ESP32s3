#include "as608_driver.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

// AS608 Protocol constants
#define AS608_START_CODE 0xEF01
#define AS608_ADDRESS 0xFFFFFFFF
#define AS608_COMMAND_GEN_IMG 0x01
#define AS608_COMMAND_IMG2TZ 0x02
#define AS608_COMMAND_MATCH 0x03
#define AS608_COMMAND_STORE 0x06
#define AS608_COMMAND_LOAD 0x07
#define AS608_COMMAND_DELETE 0x0C
#define AS608_COMMAND_EMPTY 0x0D
#define AS608_COMMAND_SEARCH 0x04

#define AS608_ACK_SUCCESS 0x00
#define AS608_ACK_FAIL 0x01

// Internal structures
typedef struct {
    uart_port_t uart_num;
    QueueHandle_t event_queue;
    TaskHandle_t task_handle;
    nvs_handle_t nvs_handle;
} as608_driver_t;

static as608_driver_t driver = {0};
static const char *TAG = "AS608_DRIVER";

// Event base definition
ESP_EVENT_DEFINE_BASE(AS608_EVENT);

// Helper functions
static esp_err_t send_command(uint8_t *cmd, size_t len) {
    uint8_t packet[12 + len];
    uint16_t checksum = 0;

    // Start code
    packet[0] = (AS608_START_CODE >> 8) & 0xFF;
    packet[1] = AS608_START_CODE & 0xFF;

    // Address
    for (int i = 0; i < 4; i++) {
        packet[2 + i] = (AS608_ADDRESS >> (24 - i * 8)) & 0xFF;
    }

    // Packet ID (command)
    packet[6] = 0x01;

    // Length
    packet[7] = ((len + 2) >> 8) & 0xFF;
    packet[8] = (len + 2) & 0xFF;

    // Command data
    memcpy(&packet[9], cmd, len);

    // Checksum
    for (int i = 6; i < 9 + len; i++) {
        checksum += packet[i];
    }
    packet[9 + len] = (checksum >> 8) & 0xFF;
    packet[10 + len] = checksum & 0xFF;

    return uart_write_bytes(driver.uart_num, (const char *)packet, sizeof(packet));
}

static esp_err_t receive_response(uint8_t *response, size_t *len, TickType_t timeout) {
    // Simplified: assume fixed response size for now
    return uart_read_bytes(driver.uart_num, response, *len, timeout);
}

// Task to handle UART communication and events
static void as608_task(void *arg) {
    uint8_t buffer[128];
    size_t len;

    while (1) {
        // Wait for fingerprint detection or command
        // For simplicity, poll or use interrupt
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Public API implementations
esp_err_t as608_driver_init(const as608_config_t *config) {
    esp_err_t ret;

    // Initialize UART
    uart_config_t uart_config = {
        .baud_rate = config->baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ret = uart_driver_install(config->uart_num, config->buffer_size, 0, 0, NULL, 0);
    if (ret != ESP_OK) return ret;

    ret = uart_param_config(config->uart_num, &uart_config);
    if (ret != ESP_OK) return ret;

    ret = uart_set_pin(config->uart_num, config->tx_pin, config->rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) return ret;

    driver.uart_num = config->uart_num;

    // Initialize NVS
    ret = nvs_flash_init();
    if (ret != ESP_OK) return ret;

    ret = nvs_open("as608", NVS_READWRITE, &driver.nvs_handle);
    if (ret != ESP_OK) return ret;

    // Create event loop if not exists
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;

    // Create task
    xTaskCreate(as608_task, "as608_task", 4096, NULL, 5, &driver.task_handle);

    ESP_LOGI(TAG, "AS608 driver initialized");
    return ESP_OK;
}

esp_err_t as608_driver_deinit() {
    if (driver.task_handle) {
        vTaskDelete(driver.task_handle);
    }
    uart_driver_delete(driver.uart_num);
    nvs_close(driver.nvs_handle);
    return ESP_OK;
}

esp_err_t as608_enroll_fingerprint(uint16_t id, const char *name) {
    // Simplified enroll process
    // Send genImg, img2Tz, store, etc.
    // For now, just store in NVS
    char key[16];
    snprintf(key, sizeof(key), "id_%d", id);
    esp_err_t ret = nvs_set_str(driver.nvs_handle, key, name);
    if (ret != ESP_OK) return ret;
    ret = nvs_commit(driver.nvs_handle);
    if (ret != ESP_OK) return ret;

    esp_event_post(AS608_EVENT, AS608_EVENT_ENROLL_SUCCESS, &id, sizeof(id), portMAX_DELAY);
    return ESP_OK;
}

esp_err_t as608_verify_fingerprint() {
    // Simplified verify
    // Assume match for demo
    uint16_t id = 1;
    esp_event_post(AS608_EVENT, AS608_EVENT_MATCH_FOUND, &id, sizeof(id), portMAX_DELAY);
    return ESP_OK;
}

esp_err_t as608_delete_fingerprint(uint16_t id) {
    char key[16];
    snprintf(key, sizeof(key), "id_%d", id);
    esp_err_t ret = nvs_erase_key(driver.nvs_handle, key);
    if (ret != ESP_OK) return ret;
    ret = nvs_commit(driver.nvs_handle);
    if (ret != ESP_OK) return ret;

    esp_event_post(AS608_EVENT, AS608_EVENT_DELETE_SUCCESS, &id, sizeof(id), portMAX_DELAY);
    return ESP_OK;
}

esp_err_t as608_get_stored_ids(as608_id_t *ids, size_t *count, size_t max_count) {
    // Simplified: iterate NVS keys
    // NVS doesn't support easy listing, so this is placeholder
    *count = 0;
    return ESP_OK;
}

bool as608_id_exists(uint16_t id) {
    char key[16];
    snprintf(key, sizeof(key), "id_%d", id);
    size_t len;
    return nvs_get_str(driver.nvs_handle, key, NULL, &len) == ESP_OK;
}