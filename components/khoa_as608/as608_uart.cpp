/**
 * @file as608_uart.cpp
 * @brief UART Driver implementation for AS608 Fingerprint Sensor
 */

#include "as608_uart.hpp"
#include <esp_log.h>
#include <esp_timer.h>
#include <cstring>

namespace as608 {

static const char* TAG = "AS608_UART";

AS608Uart::AS608Uart()
    : m_initialized(false)
    , m_uartNum(UART_NUM_MAX) {
    std::memset(m_txBuffer, 0, sizeof(m_txBuffer));
    std::memset(m_rxBuffer, 0, sizeof(m_rxBuffer));
}

AS608Uart::~AS608Uart() {
    deinit();
}

esp_err_t AS608Uart::init(const UartConfig& config) {
    if (m_initialized) {
        ESP_LOGW(TAG, "Already initialized, deinitializing first");
        deinit();
    }
    
    ESP_LOGI(TAG, "Init UART%d TX=%d RX=%d baud=%d",
             config.uart_num, config.tx_pin, config.rx_pin, config.baudrate);
    
    uart_config_t uart_config = {
        .baud_rate = config.baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {0},
    };
    
    esp_err_t ret = uart_param_config(config.uart_num, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = uart_set_pin(config.uart_num, config.tx_pin, config.rx_pin,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = uart_driver_install(config.uart_num, RX_BUFFER_SIZE, 0, 0, nullptr, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    m_uartNum = config.uart_num;
    m_initialized = true;
    
    return ESP_OK;
}

void AS608Uart::deinit() {
    if (m_initialized && m_uartNum < UART_NUM_MAX) {
        uart_driver_delete(m_uartNum);
        m_uartNum = UART_NUM_MAX;
        m_initialized = false;
    }
}

esp_err_t AS608Uart::send(const uint8_t* data, size_t len) {
    if (!m_initialized) {
        ESP_LOGE(TAG, "UART not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGD(TAG, "TX [%d]: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X...",
             (int)len,
             len > 0 ? data[0] : 0, len > 1 ? data[1] : 0,
             len > 2 ? data[2] : 0, len > 3 ? data[3] : 0,
             len > 4 ? data[4] : 0, len > 5 ? data[5] : 0,
             len > 6 ? data[6] : 0, len > 7 ? data[7] : 0,
             len > 8 ? data[8] : 0, len > 9 ? data[9] : 0);
    
    // Flush RX buffer before sending
    flushRx();
    
    int written = uart_write_bytes(m_uartNum, data, len);
    if (written != static_cast<int>(len)) {
        ESP_LOGE(TAG, "UART write failed: %d/%d", written, (int)len);
        return ESP_FAIL;
    }
    
    // Wait for TX complete
    esp_err_t ret = uart_wait_tx_done(m_uartNum, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART TX timeout");
        return ret;
    }
    
    return ESP_OK;
}

esp_err_t AS608Uart::receive(uint8_t* buffer, size_t buffer_size, size_t* bytes_read, int timeout_ms) {
    if (!m_initialized) {
        ESP_LOGE(TAG, "UART not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    std::memset(buffer, 0, buffer_size);
    
    size_t total_read = 0;
    int64_t start = esp_timer_get_time() / 1000;
    
    // Read at least header (12 bytes minimum for AS608)
    while (total_read < 12) {
        int64_t elapsed = (esp_timer_get_time() / 1000) - start;
        if (elapsed > timeout_ms) {
            ESP_LOGW(TAG, "RX timeout: got %d bytes", (int)total_read);
            *bytes_read = total_read;
            return ESP_ERR_TIMEOUT;
        }
        
        int remaining_timeout = timeout_ms - static_cast<int>(elapsed);
        int len = uart_read_bytes(m_uartNum, buffer + total_read,
                                  buffer_size - total_read,
                                  pdMS_TO_TICKS(remaining_timeout > 100 ? 100 : remaining_timeout));
        if (len > 0) {
            total_read += len;
        }
    }
    
    ESP_LOGD(TAG, "RX [%d]: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
             (int)total_read,
             buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5],
             buffer[6], buffer[7], buffer[8], buffer[9], buffer[10], buffer[11]);
    
    *bytes_read = total_read;
    return ESP_OK;
}

void AS608Uart::flushRx() {
    if (m_initialized && m_uartNum < UART_NUM_MAX) {
        uart_flush_input(m_uartNum);
    }
}

} // namespace as608
