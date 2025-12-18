/*
 * AS608 Sensor Low-Level Implementation
 */

#include "as608_sensor.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <cstring>
#include <esp_intr_alloc.h>

static const char *TAG = "AS608Sensor";

#define AS608_TIMEOUT_MS    1000
#define AS608_INTER_BYTE_TIMEOUT_MS  100

AS608Sensor::AS608Sensor() = default;

AS608Sensor::~AS608Sensor() {
    if (is_initialized_) {
        uart_driver_delete(uart_port_);
        is_initialized_ = false;
    }
}

esp_err_t AS608Sensor::Initialize(int uart_num, int tx_pin, int rx_pin, int baudrate) {
    if (is_initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Init UART%d TX=%d RX=%d baud=%d",
             uart_num, tx_pin, rx_pin, baudrate);

    uart_config_t uart_config = {
        .baud_rate = baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_port_t port = (uart_port_t)uart_num;
    esp_err_t ret = uart_param_config(port, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_set_pin(port, tx_pin, rx_pin,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_driver_install(port, 1024, 0, 0, NULL, ESP_INTR_FLAG_IRAM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    uart_port_ = port;
    is_initialized_ = true;
    return ESP_OK;
}

void AS608Sensor::Deinitialize() {
    if (is_initialized_) {
        uart_driver_delete(uart_port_);
        is_initialized_ = false;
    }
}

bool AS608Sensor::IsInitialized() const {
    return is_initialized_;
}

esp_err_t AS608Sensor::SendCommand(uint8_t cmd, const uint8_t* params, size_t params_len) {
    if (!IsInitialized()) return ESP_ERR_INVALID_STATE;

    size_t pkt_len = BuildCommandPacket(tx_buf_, cmd, params, params_len);

    /* Debug log */
    ESP_LOGD(TAG, "TX [%d]: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X...",
             (int)pkt_len,
             tx_buf_[0], tx_buf_[1], tx_buf_[2], tx_buf_[3], tx_buf_[4],
             tx_buf_[5], tx_buf_[6], tx_buf_[7], tx_buf_[8], tx_buf_[9]);

    /* Flush RX buffer before sending */
    uart_flush_input(uart_port_);

    int written = uart_write_bytes(uart_port_, (const char *)tx_buf_, pkt_len);
    if (written != (int)pkt_len) {
        ESP_LOGE(TAG, "UART write failed: %d/%d", written, (int)pkt_len);
        return ESP_FAIL;
    }

    /* Wait for TX complete */
    esp_err_t ret = uart_wait_tx_done(uart_port_, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART TX timeout");
        return ret;
    }

    return ESP_OK;
}

esp_err_t AS608Sensor::ReceiveResponse(uint8_t* confirm, const uint8_t** data, size_t* data_len, int timeout_ms) {
    if (!IsInitialized()) return ESP_ERR_INVALID_STATE;

    memset(rx_buf_, 0, sizeof(rx_buf_));

    /* Read response with timeout */
    int total_read = 0;
    int64_t start = esp_timer_get_time() / 1000;

    /* Read at least header first */
    while (total_read < 12) {
        int64_t elapsed = (esp_timer_get_time() / 1000) - start;
        if (elapsed > timeout_ms) {
            ESP_LOGW(TAG, "RX timeout: got %d bytes", total_read);
            return ESP_ERR_TIMEOUT;
        }

        int remaining_timeout = timeout_ms - elapsed;
        int len = uart_read_bytes(uart_port_, rx_buf_ + total_read,
                                   sizeof(rx_buf_) - total_read,
                                   pdMS_TO_TICKS(remaining_timeout > 100 ? 100 : remaining_timeout));
        if (len > 0) {
            total_read += len;
        }
    }

    /* Debug log */
    ESP_LOGD(TAG, "RX [%d]: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
             total_read,
             rx_buf_[0], rx_buf_[1], rx_buf_[2], rx_buf_[3], rx_buf_[4], rx_buf_[5],
             rx_buf_[6], rx_buf_[7], rx_buf_[8], rx_buf_[9], rx_buf_[10], rx_buf_[11]);

    /* Parse response */
    esp_err_t ret = ParseResponse(rx_buf_, total_read, confirm, data, data_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Parse failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGD(TAG, "Confirm: 0x%02X (%s)", *confirm, ConfirmString(*confirm));
    return ESP_OK;
}

size_t AS608Sensor::BuildCommandPacket(uint8_t* buf, uint8_t cmd, const uint8_t* params, size_t params_len) {
    // AS608 packet format:
    // Header: 0xEF01 (2 bytes)
    // Address: 0xFFFFFFFF (4 bytes)
    // Identifier: 0x01 (1 byte, command)
    // Length: params_len + 3 (2 bytes, big endian)  // cmd + params + checksum
    // Cmd: cmd (1 byte)
    // Params: params (params_len bytes)
    // Checksum: sum of cmd + params (2 bytes, big endian)

    buf[0] = 0xEF;
    buf[1] = 0x01;
    buf[2] = 0xFF;
    buf[3] = 0xFF;
    buf[4] = 0xFF;
    buf[5] = 0xFF;
    buf[6] = 0x01;  // Identifier

    uint16_t length = 1 + params_len + 2;  // cmd + params + checksum
    buf[7] = (length >> 8) & 0xFF;
    buf[8] = length & 0xFF;

    buf[9] = cmd;

    uint16_t checksum = cmd;
    for (size_t i = 0; i < params_len; i++) {
        buf[10 + i] = params[i];
        checksum += params[i];
    }

    buf[10 + params_len] = (checksum >> 8) & 0xFF;
    buf[11 + params_len] = checksum & 0xFF;

    return 12 + params_len;
}

esp_err_t AS608Sensor::ParseResponse(const uint8_t* buf, size_t len, uint8_t* confirm, const uint8_t** data, size_t* data_len) {
    if (len < 12) return ESP_ERR_INVALID_SIZE;

    // Check header
    if (buf[0] != 0xEF || buf[1] != 0x01) return ESP_ERR_INVALID_RESPONSE;

    if (buf[6] != 0x07) return ESP_ERR_INVALID_RESPONSE;  // Response identifier

    uint16_t length = (buf[7] << 8) | buf[8];
    if (len < 9 + length) return ESP_ERR_INVALID_SIZE;

    *confirm = buf[9];

    if (data && data_len) {
        *data = buf + 10;
        *data_len = length - 3;  // confirm + data + checksum
    }

    // Checksum
    uint16_t checksum = *confirm;
    for (size_t i = 0; i < length - 3; i++) {
        checksum += buf[10 + i];
    }
    uint16_t expected = (buf[9 + length - 2] << 8) | buf[9 + length - 1];
    if (checksum != expected) return ESP_ERR_INVALID_CRC;

    return ESP_OK;
}

const char* AS608Sensor::ConfirmString(uint8_t confirm) {
    switch (confirm) {
        case 0x00: return "OK";
        case 0x01: return "PACKET_RCV_ERR";
        case 0x02: return "NO_FINGER";
        case 0x03: return "IMAGE_FAIL";
        case 0x04: return "IMAGE_MESSY";
        case 0x05: return "FEATURE_FAIL";
        case 0x06: return "ENROLL_MISMATCH";
        case 0x07: return "BAD_LOCATION";
        case 0x08: return "DB_RANGE_FAIL";
        case 0x09: return "UPLOAD_FEATURE_FAIL";
        case 0x0A: return "PACKET_RESPONSE_FAIL";
        case 0x0B: return "UPLOAD_FAIL";
        case 0x0C: return "DELETE_FAIL";
        case 0x0D: return "DBCLEAR_FAIL";
        case 0x0E: return "BAD_PASSWORD";
        case 0x0F: return "INVALID_IMAGE";
        case 0x10: return "FLASH_ERR";
        case 0x11: return "NO_RESPONSE_FAIL";
        case 0x12: return "ADDR_CODE_INVALID";
        case 0x13: return "PASSWORD_VERIFY_FAIL";
        case 0x14: return "FLASH_WRITE_ERR";
        case 0x15: return "NO_CONTENT_IN_FLASH";
        case 0x18: return "INVALID_REG";
        case 0x19: return "INCORRECT_CONFIG";
        case 0x1A: return "INVALID_PARAM";
        default: return "UNKNOWN";
    }
}