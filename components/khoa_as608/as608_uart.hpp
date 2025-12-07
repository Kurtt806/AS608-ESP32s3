/**
 * @file as608_uart.hpp
 * @brief UART Driver for AS608 Fingerprint Sensor
 * 
 * Encapsulates ESP-IDF UART driver operations for AS608 communication.
 */

#ifndef AS608_UART_HPP
#define AS608_UART_HPP

#include <cstdint>
#include <cstddef>
#include <esp_err.h>
#include <driver/uart.h>

namespace as608 {

/**
 * @brief UART configuration for AS608
 */
struct UartConfig {
    uart_port_t uart_num;
    int tx_pin;
    int rx_pin;
    int baudrate;
    
    UartConfig() : uart_num(UART_NUM_1), tx_pin(17), rx_pin(16), baudrate(57600) {}
    UartConfig(uart_port_t num, int tx, int rx, int baud)
        : uart_num(num), tx_pin(tx), rx_pin(rx), baudrate(baud) {}
};

/**
 * @brief UART Driver class for AS608 sensor communication
 * 
 * Handles low-level UART operations including initialization,
 * sending, and receiving data with timeout support.
 */
class AS608Uart {
public:
    static constexpr size_t BUFFER_SIZE = 256;
    static constexpr int DEFAULT_TIMEOUT_MS = 1000;
    static constexpr int RX_BUFFER_SIZE = 1024;
    
    AS608Uart();
    ~AS608Uart();
    
    // Non-copyable
    AS608Uart(const AS608Uart&) = delete;
    AS608Uart& operator=(const AS608Uart&) = delete;
    
    /**
     * @brief Initialize UART with given configuration
     * @param config UART configuration
     * @return ESP_OK on success
     */
    esp_err_t init(const UartConfig& config);
    
    /**
     * @brief Deinitialize UART driver
     */
    void deinit();
    
    /**
     * @brief Check if UART is initialized
     * @return true if initialized
     */
    bool isInitialized() const { return m_initialized; }
    
    /**
     * @brief Send data over UART
     * @param data Data buffer to send
     * @param len Number of bytes to send
     * @return ESP_OK on success
     */
    esp_err_t send(const uint8_t* data, size_t len);
    
    /**
     * @brief Receive data from UART
     * @param buffer Output buffer
     * @param buffer_size Size of output buffer
     * @param bytes_read Number of bytes actually read
     * @param timeout_ms Read timeout in milliseconds
     * @return ESP_OK on success, ESP_ERR_TIMEOUT on timeout
     */
    esp_err_t receive(uint8_t* buffer, size_t buffer_size, size_t* bytes_read, int timeout_ms = DEFAULT_TIMEOUT_MS);
    
    /**
     * @brief Flush receive buffer
     */
    void flushRx();
    
    /**
     * @brief Get internal TX buffer for building packets
     * @return Pointer to TX buffer
     */
    uint8_t* getTxBuffer() { return m_txBuffer; }
    
    /**
     * @brief Get internal RX buffer for parsing responses
     * @return Pointer to RX buffer
     */
    uint8_t* getRxBuffer() { return m_rxBuffer; }

private:
    bool m_initialized;
    uart_port_t m_uartNum;
    uint8_t m_txBuffer[BUFFER_SIZE];
    uint8_t m_rxBuffer[BUFFER_SIZE];
};

} // namespace as608

#endif // AS608_UART_HPP
