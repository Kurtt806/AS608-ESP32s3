/**
 * @file as608.c
 * @brief AS608 Fingerprint Sensor Driver Implementation
 * 
 * ESP-IDF component for AS608 optical fingerprint sensor
 * Based on industry-standard practices from Adafruit Fingerprint Library
 * 
 * Features:
 * - Full UART packet framing with checksum verification
 * - Robust error handling and retry mechanism
 * - Support for all standard AS608 commands
 * - LED control for visual feedback (if sensor supports it)
 * - Template management (store, load, delete, search)
 * - System parameter configuration
 * 
 * @author Based on Adafruit Fingerprint Sensor Library
 * @date 2025
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "as608.h"

/* ============================================================================
 * Private Definitions
 * ============================================================================ */

static const char *TAG = "AS608";

/** @brief AS608 packet start code (header) */
#define AS608_START_CODE        0xEF01

/** @brief AS608 packet header bytes */
#define AS608_HEADER_HIGH       0xEF
#define AS608_HEADER_LOW        0x01

/** @brief Packet structure sizes */
#define AS608_HEADER_SIZE       2       /**< Header size (0xEF01) */
#define AS608_ADDRESS_SIZE      4       /**< Address size */
#define AS608_PID_SIZE          1       /**< Packet ID size */
#define AS608_LENGTH_SIZE       2       /**< Length field size */
#define AS608_CHECKSUM_SIZE     2       /**< Checksum size */
#define AS608_MIN_PACKET_SIZE   12      /**< Minimum packet size */

/** @brief Maximum packet data size */
#define AS608_MAX_DATA_SIZE     256

/** @brief UART buffer sizes */
#define AS608_UART_RX_BUF_SIZE  2048
#define AS608_UART_TX_BUF_SIZE  1024

/** @brief Command retry count */
#define AS608_CMD_RETRY_COUNT   2

/** @brief Delay after sending command (ms) - sensor processing time */
#define AS608_CMD_DELAY_MS      200

/** @brief Default timeout for commands (ms) */
#define AS608_DEFAULT_TIMEOUT   2000

/* ============================================================================
 * Helper Macros
 * ============================================================================ */

/** @brief Extract high byte from uint16_t */
#define HIGH_BYTE(x)    (((x) >> 8) & 0xFF)

/** @brief Extract low byte from uint16_t */
#define LOW_BYTE(x)     ((x) & 0xFF)

/** @brief Check if buffer ID is valid */
#define IS_VALID_BUFFER(id)     ((id) >= 1 && (id) <= 2)

/** @brief Check if page ID is in range */
#define IS_VALID_PAGE(id)       ((id) < s_config.library_size)

/* ============================================================================
 * Private Variables
 * ============================================================================ */

static as608_config_t s_config;
static bool s_initialized = false;

/* ============================================================================
 * Private Functions - Packet Handling
 * ============================================================================ */

/**
 * @brief Build AS608 command packet
 * @param buf Output buffer
 * @param pid Packet identifier
 * @param data Command data
 * @param data_len Data length
 * @return Total packet length
 */
static size_t as608_build_packet(uint8_t *buf, uint8_t pid, const uint8_t *data, size_t data_len)
{
    size_t idx = 0;
    uint16_t length = data_len + 2; // data + checksum
    uint16_t checksum = 0;

    // Header
    buf[idx++] = AS608_HEADER_HIGH;
    buf[idx++] = AS608_HEADER_LOW;

    // Address (4 bytes, big-endian)
    buf[idx++] = (s_config.device_address >> 24) & 0xFF;
    buf[idx++] = (s_config.device_address >> 16) & 0xFF;
    buf[idx++] = (s_config.device_address >> 8) & 0xFF;
    buf[idx++] = s_config.device_address & 0xFF;

    // Packet ID
    buf[idx++] = pid;
    checksum += pid;

    // Length (2 bytes, big-endian)
    buf[idx++] = HIGH_BYTE(length);
    buf[idx++] = LOW_BYTE(length);
    checksum += HIGH_BYTE(length);
    checksum += LOW_BYTE(length);

    // Data
    for (size_t i = 0; i < data_len; i++) {
        buf[idx++] = data[i];
        checksum += data[i];
    }

    // Checksum (2 bytes, big-endian)
    buf[idx++] = HIGH_BYTE(checksum);
    buf[idx++] = LOW_BYTE(checksum);

    return idx;
}

/**
 * @brief Send command and receive response
 * @param cmd_data Command data buffer
 * @param cmd_len Command data length
 * @param resp_data Response data buffer (excluding header/checksum)
 * @param resp_len Pointer to response length
 * @param timeout_ms Timeout in milliseconds
 * @return ESP_OK on success, error code otherwise
 */
static esp_err_t as608_send_cmd(const uint8_t *cmd_data, size_t cmd_len, 
                                 uint8_t *resp_data, size_t *resp_len, 
                                 uint32_t timeout_ms)
{
    uint8_t tx_buf[AS608_MAX_DATA_SIZE + 12];
    uint8_t rx_buf[AS608_MAX_DATA_SIZE + 64];  // Extra space for sync searching
    size_t tx_len;

    // Build packet
    tx_len = as608_build_packet(tx_buf, AS608_PID_COMMAND, cmd_data, cmd_len);

    ESP_LOGD(TAG, "TX packet (%d bytes):", tx_len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, tx_buf, tx_len, ESP_LOG_DEBUG);

    // Clear RX buffer only if there's old data
    size_t buffered_len = 0;
    uart_get_buffered_data_len(s_config.uart_port, &buffered_len);
    if (buffered_len > 0) {
        ESP_LOGD(TAG, "Clearing %d bytes from RX buffer", buffered_len);
        uart_flush_input(s_config.uart_port);
        vTaskDelay(pdMS_TO_TICKS(20));  // Wait for UART buffer to stabilize
    }

    // Send command
    int written = uart_write_bytes(s_config.uart_port, tx_buf, tx_len);
    if (written != tx_len) {
        ESP_LOGE(TAG, "UART write failed: %d/%d", written, tx_len);
        return ESP_FAIL;
    }

    // Wait for TX to complete and sensor to process
    uart_wait_tx_done(s_config.uart_port, pdMS_TO_TICKS(200));
    vTaskDelay(pdMS_TO_TICKS(AS608_CMD_DELAY_MS));

    // Read header first (9 bytes: header(2) + address(4) + PID(1) + length(2))
    int rx_len = 0;
    int header_offset = 0;
    
    // Read with timeout for initial response
    int bytes_read = uart_read_bytes(s_config.uart_port, rx_buf, 9, pdMS_TO_TICKS(timeout_ms));
    if (bytes_read < 9) {
        // Try to read more if we got partial data
        if (bytes_read > 0 && bytes_read < 9) {
            int more = uart_read_bytes(s_config.uart_port, rx_buf + bytes_read, 
                                       9 - bytes_read, pdMS_TO_TICKS(400));
            bytes_read += more;
        }
        if (bytes_read < 9) {
            ESP_LOGW(TAG, "Response timeout or too short: %d bytes", bytes_read);
            if (bytes_read > 0) {
                ESP_LOG_BUFFER_HEX_LEVEL(TAG, rx_buf, bytes_read, ESP_LOG_DEBUG);
            }
            return ESP_ERR_TIMEOUT;
        }
    }
    
    // Search for valid header in received bytes
    bool header_found = false;
    for (int i = 0; i < bytes_read - 1; i++) {
        if (rx_buf[i] == AS608_HEADER_HIGH && rx_buf[i + 1] == AS608_HEADER_LOW) {
            header_offset = i;
            header_found = true;
            break;
        }
    }
    
    // If no header found, try to read more data
    if (!header_found) {
        ESP_LOGD(TAG, "Header not found in first %d bytes, reading more...", bytes_read);
        int more = uart_read_bytes(s_config.uart_port, rx_buf + bytes_read, 
                                   16, pdMS_TO_TICKS(600));
        if (more > 0) {
            bytes_read += more;
            for (int i = 0; i < bytes_read - 1; i++) {
                if (rx_buf[i] == AS608_HEADER_HIGH && rx_buf[i + 1] == AS608_HEADER_LOW) {
                    header_offset = i;
                    header_found = true;
                    break;
                }
            }
        }
        if (!header_found) {
            ESP_LOGW(TAG, "No valid header found in %d bytes", bytes_read);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, rx_buf, bytes_read, ESP_LOG_DEBUG);
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    
    // If header not at start, shift data and read remaining
    if (header_offset > 0) {
        ESP_LOGD(TAG, "Header found at offset %d", header_offset);
        int remaining = bytes_read - header_offset;
        memmove(rx_buf, rx_buf + header_offset, remaining);
        int need_more = 9 - remaining;
        if (need_more > 0) {
            int more = uart_read_bytes(s_config.uart_port, rx_buf + remaining, need_more, pdMS_TO_TICKS(timeout_ms));
            if (more < need_more) {
                ESP_LOGW(TAG, "Failed to read complete header after sync");
                return ESP_ERR_TIMEOUT;
            }
        }
    }
    rx_len = 9;

    // Validate header
    if (rx_buf[0] != AS608_HEADER_HIGH || rx_buf[1] != AS608_HEADER_LOW) {
        ESP_LOGW(TAG, "Invalid header: 0x%02X 0x%02X", rx_buf[0], rx_buf[1]);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, rx_buf, bytes_read, ESP_LOG_DEBUG);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Validate packet ID (should be ACK)
    if (rx_buf[6] != AS608_PID_ACK) {
        ESP_LOGW(TAG, "Invalid PID: 0x%02X (expected ACK 0x07)", rx_buf[6]);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Get data length from packet (includes data + checksum)
    uint16_t pkt_length = (rx_buf[7] << 8) | rx_buf[8];
    
    // Validate packet length
    if (pkt_length > AS608_MAX_DATA_SIZE) {
        ESP_LOGW(TAG, "Packet length too large: %d", pkt_length);
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Read remaining data (pkt_length bytes = data + 2 bytes checksum)
    if (pkt_length > 0) {
        int read_more = uart_read_bytes(s_config.uart_port, rx_buf + 9, pkt_length,
                                        pdMS_TO_TICKS(timeout_ms));
        if (read_more < pkt_length) {
            // Try to read more if partial
            if (read_more > 0) {
                int extra = uart_read_bytes(s_config.uart_port, rx_buf + 9 + read_more,
                                           pkt_length - read_more, pdMS_TO_TICKS(300));
                read_more += extra;
            }
        }
        if (read_more != pkt_length) {
            ESP_LOGW(TAG, "Failed to read data: %d/%d", read_more, pkt_length);
            return ESP_ERR_TIMEOUT;
        }
        rx_len += read_more;
    }

    ESP_LOGD(TAG, "RX packet (%d bytes):", rx_len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, rx_buf, rx_len, ESP_LOG_DEBUG);

    // Calculate and verify checksum
    uint16_t calc_checksum = 0;
    for (int i = 6; i < rx_len - 2; i++) {
        calc_checksum += rx_buf[i];
    }
    uint16_t recv_checksum = (rx_buf[rx_len - 2] << 8) | rx_buf[rx_len - 1];

    if (calc_checksum != recv_checksum) {
        ESP_LOGE(TAG, "Checksum mismatch: calc=0x%04X recv=0x%04X", calc_checksum, recv_checksum);
        return ESP_ERR_INVALID_CRC;
    }

    // Extract data (skip header, address, PID, length; exclude checksum)
    size_t data_len = pkt_length - 2; // exclude checksum from length
    if (resp_data && data_len > 0) {
        memcpy(resp_data, &rx_buf[9], data_len);
    }
    if (resp_len) {
        *resp_len = data_len;
    }

    return ESP_OK;
}

/**
 * @brief Convert AS608 confirmation code to ESP error
 */
static esp_err_t as608_code_to_err(uint8_t code)
{
    switch (code) {
        case AS608_OK:
            return ESP_OK;
        case AS608_ERR_NO_FINGER:
            return ESP_ERR_NOT_FOUND;
        case AS608_ERR_NOT_FOUND:
        case AS608_ERR_NO_MATCH:
            return ESP_ERR_NOT_FOUND;
        case AS608_ERR_BAD_LOCATION:
            return ESP_ERR_INVALID_ARG;
        default:
            return ESP_FAIL;
    }
}

/* ============================================================================
 * Public Functions
 * ============================================================================ */

esp_err_t as608_init(const as608_config_t *config)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    // Use default config if NULL
    if (config == NULL) {
        s_config = (as608_config_t)AS608_CONFIG_DEFAULT();
    } else {
        s_config = *config;
    }

    ESP_LOGI(TAG, "Initializing AS608 on UART%d (TX:%d, RX:%d, Baud:%ld)",
             s_config.uart_port, s_config.tx_gpio, s_config.rx_gpio, (long)s_config.baud_rate);

    // UART configuration
    uart_config_t uart_config = {
        .baud_rate = (int)s_config.baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_param_config(s_config.uart_port, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART param config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_set_pin(s_config.uart_port, s_config.tx_gpio, s_config.rx_gpio, 
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_driver_install(s_config.uart_port, AS608_UART_RX_BUF_SIZE * 2, 
                               AS608_UART_TX_BUF_SIZE * 2, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Configure optional RST pin
    if (s_config.rst_gpio != GPIO_NUM_NC) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << s_config.rst_gpio),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        gpio_set_level(s_config.rst_gpio, 1);
    }

    // Configure optional power enable pin
    if (s_config.pwr_en_gpio != GPIO_NUM_NC) {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << s_config.pwr_en_gpio),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        gpio_set_level(s_config.pwr_en_gpio, 1);
        vTaskDelay(pdMS_TO_TICKS(100)); // Wait for sensor to power up
    }

    // Give sensor time to initialize
    vTaskDelay(pdMS_TO_TICKS(200));

    s_initialized = true;

    // Verify password (handshake)
    ret = as608_verify_password(s_config.password);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Verify password failed, trying handshake...");
        ret = as608_handshake();
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "AS608 initialized successfully");
    } else {
        ESP_LOGW(TAG, "AS608 init: sensor may not be connected");
    }

    return ESP_OK; // Return OK even if handshake fails (sensor might be connected later)
}

uint16_t as608_get_library_size(void)
{
    return s_config.library_size;
}

esp_err_t as608_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    esp_err_t ret = uart_driver_delete(s_config.uart_port);
    s_initialized = false;
    
    ESP_LOGI(TAG, "AS608 deinitialized");
    return ret;
}

esp_err_t as608_get_image(void)
{
    uint8_t cmd[] = {AS608_CMD_GET_IMAGE};
    uint8_t resp[8];
    size_t resp_len;

    esp_err_t ret = as608_send_cmd(cmd, sizeof(cmd), resp, &resp_len, s_config.timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    if (resp_len < 1) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t conf_code = resp[0];
    ESP_LOGD(TAG, "GetImage confirmation: 0x%02X", conf_code);

    if (conf_code != AS608_OK) {
        if (conf_code == AS608_ERR_NO_FINGER) {
            ESP_LOGD(TAG, "No finger detected");
        } else {
            ESP_LOGW(TAG, "GetImage error: %s", as608_err_to_str(conf_code));
        }
    }

    return as608_code_to_err(conf_code);
}

esp_err_t as608_gen_char(uint8_t buf_id)
{
    if (!IS_VALID_BUFFER(buf_id)) {
        ESP_LOGE(TAG, "Invalid buffer ID: %d (must be 1 or 2)", buf_id);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t cmd[] = {AS608_CMD_GEN_CHAR, buf_id};
    uint8_t resp[8];
    size_t resp_len;
    esp_err_t ret;

    /* GenChar takes longer - use extended timeout with retry */
    for (int retry = 0; retry < AS608_CMD_RETRY_COUNT; retry++) {
        if (retry > 0) {
            ESP_LOGW(TAG, "GenChar retry %d", retry);
            vTaskDelay(pdMS_TO_TICKS(250));  /* Delay before retry */
        }
        
        ret = as608_send_cmd(cmd, sizeof(cmd), resp, &resp_len, s_config.timeout_ms * 2);
        if (ret == ESP_OK) {
            break;
        }
    }
    
    if (ret != ESP_OK) {
        return ret;
    }

    if (resp_len < 1) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t conf_code = resp[0];
    ESP_LOGD(TAG, "GenChar confirmation: 0x%02X", conf_code);

    if (conf_code != AS608_OK) {
        ESP_LOGW(TAG, "GenChar error: %s", as608_err_to_str(conf_code));
    }

    return as608_code_to_err(conf_code);
}

esp_err_t as608_reg_model(void)
{
    uint8_t cmd[] = {AS608_CMD_REG_MODEL};
    uint8_t resp[8];
    size_t resp_len;

    esp_err_t ret = as608_send_cmd(cmd, sizeof(cmd), resp, &resp_len, s_config.timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    if (resp_len < 1) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t conf_code = resp[0];
    ESP_LOGD(TAG, "RegModel confirmation: 0x%02X", conf_code);

    if (conf_code != AS608_OK) {
        ESP_LOGW(TAG, "RegModel error: %s", as608_err_to_str(conf_code));
    }

    return as608_code_to_err(conf_code);
}

esp_err_t as608_store(uint16_t id)
{
    if (!IS_VALID_PAGE(id)) {
        ESP_LOGE(TAG, "Invalid ID: %d (max: %d)", id, s_config.library_size - 1);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t cmd[] = {
        AS608_CMD_STORE,
        0x01,                   // Buffer ID (always 1 for template)
        HIGH_BYTE(id),          // Page ID high byte
        LOW_BYTE(id)            // Page ID low byte
    };
    uint8_t resp[8];
    size_t resp_len;

    esp_err_t ret = as608_send_cmd(cmd, sizeof(cmd), resp, &resp_len, s_config.timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    if (resp_len < 1) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t conf_code = resp[0];
    ESP_LOGD(TAG, "Store confirmation: 0x%02X", conf_code);

    if (conf_code != AS608_OK) {
        ESP_LOGW(TAG, "Store error: %s", as608_err_to_str(conf_code));
    }

    return as608_code_to_err(conf_code);
}

esp_err_t as608_search(uint16_t start_id, uint16_t count, int16_t *match_id, uint16_t *match_score)
{
    uint8_t cmd[] = {
        AS608_CMD_SEARCH,
        0x01,                       // Buffer ID
        HIGH_BYTE(start_id),        // Start page high
        LOW_BYTE(start_id),         // Start page low
        HIGH_BYTE(count),           // Count high
        LOW_BYTE(count)             // Count low
    };
    uint8_t resp[16];
    size_t resp_len;
    esp_err_t ret;

    /* Search with retry for reliability - search can take longer */
    for (int retry = 0; retry < AS608_CMD_RETRY_COUNT; retry++) {
        if (retry > 0) {
            ESP_LOGW(TAG, "Search retry %d", retry);
            vTaskDelay(pdMS_TO_TICKS(250));
        }
        
        ret = as608_send_cmd(cmd, sizeof(cmd), resp, &resp_len, s_config.timeout_ms * 2.5);
        if (ret == ESP_OK) {
            break;
        }
    }
    
    if (ret != ESP_OK) {
        if (match_id) *match_id = -1;
        if (match_score) *match_score = 0;
        return ret;
    }

    if (resp_len < 1) {
        if (match_id) *match_id = -1;
        if (match_score) *match_score = 0;
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t conf_code = resp[0];
    ESP_LOGD(TAG, "Search confirmation: 0x%02X", conf_code);

    if (conf_code == AS608_OK && resp_len >= 5) {
        uint16_t page_id = (resp[1] << 8) | resp[2];
        uint16_t score = (resp[3] << 8) | resp[4];
        
        if (match_id) *match_id = page_id;
        if (match_score) *match_score = score;
        
        ESP_LOGI(TAG, "Match found: ID=%d, Score=%d", page_id, score);
        return ESP_OK;
    }

    if (match_id) *match_id = -1;
    if (match_score) *match_score = 0;

    if (conf_code == AS608_ERR_NOT_FOUND) {
        ESP_LOGD(TAG, "No match found in library");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGW(TAG, "Search error: %s", as608_err_to_str(conf_code));
    return as608_code_to_err(conf_code);
}

esp_err_t as608_delete(uint16_t id)
{
    return as608_delete_range(id, 1);
}

esp_err_t as608_delete_range(uint16_t start_id, uint16_t count)
{
    if (!IS_VALID_PAGE(start_id)) {
        ESP_LOGE(TAG, "Invalid start ID: %d", start_id);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t cmd[] = {
        AS608_CMD_DELETE_CHAR,
        HIGH_BYTE(start_id),
        LOW_BYTE(start_id),
        HIGH_BYTE(count),
        LOW_BYTE(count)
    };
    uint8_t resp[8];
    size_t resp_len;

    esp_err_t ret = as608_send_cmd(cmd, sizeof(cmd), resp, &resp_len, s_config.timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    if (resp_len < 1) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t conf_code = resp[0];
    ESP_LOGD(TAG, "Delete confirmation: 0x%02X", conf_code);

    if (conf_code != AS608_OK) {
        ESP_LOGW(TAG, "Delete error: %s", as608_err_to_str(conf_code));
    }

    return as608_code_to_err(conf_code);
}

esp_err_t as608_empty(void)
{
    uint8_t cmd[] = {AS608_CMD_EMPTY};
    uint8_t resp[8];
    size_t resp_len;

    esp_err_t ret = as608_send_cmd(cmd, sizeof(cmd), resp, &resp_len, s_config.timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    if (resp_len < 1) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t conf_code = resp[0];
    ESP_LOGD(TAG, "Empty confirmation: 0x%02X", conf_code);

    if (conf_code != AS608_OK) {
        ESP_LOGW(TAG, "Empty error: %s", as608_err_to_str(conf_code));
    } else {
        ESP_LOGI(TAG, "Fingerprint library cleared");
    }

    return as608_code_to_err(conf_code);
}

esp_err_t as608_read_sys_param(as608_sys_param_t *param)
{
    if (param == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t cmd[] = {AS608_CMD_READ_SYS_PARAM};
    uint8_t resp[32];
    size_t resp_len;

    esp_err_t ret = as608_send_cmd(cmd, sizeof(cmd), resp, &resp_len, s_config.timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    if (resp_len < 17) {
        ESP_LOGE(TAG, "ReadSysParam: insufficient response length: %d", resp_len);
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t conf_code = resp[0];
    if (conf_code != AS608_OK) {
        ESP_LOGW(TAG, "ReadSysParam error: %s", as608_err_to_str(conf_code));
        return as608_code_to_err(conf_code);
    }

    // Parse parameters (16 bytes after confirmation code)
    param->status_reg = (resp[1] << 8) | resp[2];
    param->system_id = (resp[3] << 8) | resp[4];
    param->library_size = (resp[5] << 8) | resp[6];
    param->security_level = (resp[7] << 8) | resp[8];
    param->device_address = ((uint32_t)resp[9] << 24) | ((uint32_t)resp[10] << 16) |
                            ((uint32_t)resp[11] << 8) | resp[12];
    param->packet_size = (resp[13] << 8) | resp[14];
    param->baud_setting = (resp[15] << 8) | resp[16];

    ESP_LOGD(TAG, "SysParam: status=0x%04X, lib_size=%d, sec_level=%d, baud=%d",
             param->status_reg, param->library_size, 
             param->security_level, param->baud_setting * 9600);

    return ESP_OK;
}

esp_err_t as608_get_template_count(uint16_t *count)
{
    if (count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t cmd[] = {AS608_CMD_TEMPLATE_COUNT};
    uint8_t resp[8];
    size_t resp_len;

    esp_err_t ret = as608_send_cmd(cmd, sizeof(cmd), resp, &resp_len, s_config.timeout_ms);
    if (ret != ESP_OK) {
        *count = 0;
        return ret;
    }

    if (resp_len < 3) {
        *count = 0;
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t conf_code = resp[0];
    if (conf_code != AS608_OK) {
        ESP_LOGW(TAG, "TemplateCount error: %s", as608_err_to_str(conf_code));
        *count = 0;
        return as608_code_to_err(conf_code);
    }

    *count = (resp[1] << 8) | resp[2];
    ESP_LOGD(TAG, "Template count: %d", *count);

    return ESP_OK;
}

esp_err_t as608_verify_password(uint32_t password)
{
    uint8_t cmd[] = {
        AS608_CMD_VERIFY_PASSWORD,
        (password >> 24) & 0xFF,
        (password >> 16) & 0xFF,
        (password >> 8) & 0xFF,
        password & 0xFF
    };
    uint8_t resp[8];
    size_t resp_len;

    esp_err_t ret = as608_send_cmd(cmd, sizeof(cmd), resp, &resp_len, s_config.timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    if (resp_len < 1) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t conf_code = resp[0];
    ESP_LOGD(TAG, "VerifyPassword confirmation: 0x%02X", conf_code);

    return as608_code_to_err(conf_code);
}

esp_err_t as608_handshake(void)
{
    uint8_t cmd[] = {AS608_CMD_HANDSHAKE};
    uint8_t resp[8];
    size_t resp_len;

    esp_err_t ret = as608_send_cmd(cmd, sizeof(cmd), resp, &resp_len, 1000);
    if (ret != ESP_OK) {
        return ret;
    }

    if (resp_len < 1) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t conf_code = resp[0];
    ESP_LOGD(TAG, "Handshake confirmation: 0x%02X", conf_code);

    return as608_code_to_err(conf_code);
}

esp_err_t as608_match(uint16_t *match_score)
{
    if (match_score == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t cmd[] = {AS608_CMD_MATCH};
    uint8_t resp[8];
    size_t resp_len;

    esp_err_t ret = as608_send_cmd(cmd, sizeof(cmd), resp, &resp_len, s_config.timeout_ms);
    if (ret != ESP_OK) {
        *match_score = 0;
        return ret;
    }

    if (resp_len < 3) {
        *match_score = 0;
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t conf_code = resp[0];
    
    if (conf_code == AS608_OK) {
        *match_score = (resp[1] << 8) | resp[2];
        ESP_LOGD(TAG, "Match score: %d", *match_score);
        return ESP_OK;
    }

    *match_score = 0;
    ESP_LOGW(TAG, "Match error: %s", as608_err_to_str(conf_code));
    return as608_code_to_err(conf_code);
}

esp_err_t as608_load_char(uint8_t buf_id, uint16_t id)
{
    if (!IS_VALID_BUFFER(buf_id)) {
        ESP_LOGE(TAG, "Invalid buffer ID: %d", buf_id);
        return ESP_ERR_INVALID_ARG;
    }

    if (!IS_VALID_PAGE(id)) {
        ESP_LOGE(TAG, "Invalid page ID: %d", id);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t cmd[] = {
        AS608_CMD_LOAD_CHAR,
        buf_id,
        HIGH_BYTE(id),
        LOW_BYTE(id)
    };
    uint8_t resp[8];
    size_t resp_len;

    esp_err_t ret = as608_send_cmd(cmd, sizeof(cmd), resp, &resp_len, s_config.timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    if (resp_len < 1) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t conf_code = resp[0];
    ESP_LOGD(TAG, "LoadChar confirmation: 0x%02X", conf_code);

    if (conf_code != AS608_OK) {
        ESP_LOGW(TAG, "LoadChar error: %s", as608_err_to_str(conf_code));
    }

    return as608_code_to_err(conf_code);
}

esp_err_t as608_upload_char(uint8_t buf_id, uint8_t *char_buffer)
{
    // Upload character file - implementation requires data packet handling
    // This is a complex operation that needs multiple packets
    ESP_LOGW(TAG, "upload_char not fully implemented yet");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t as608_download_char(uint8_t buf_id, const uint8_t *char_buffer)
{
    // Download character file - implementation requires data packet handling
    ESP_LOGW(TAG, "download_char not fully implemented yet");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t as608_upload_image(uint8_t *img_buffer, size_t buf_size, size_t *actual_size)
{
    // Upload image - implementation requires data packet handling
    ESP_LOGW(TAG, "upload_image not fully implemented yet");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t as608_download_image(const uint8_t *img_buffer, size_t img_size)
{
    // Download image - implementation requires data packet handling
    ESP_LOGW(TAG, "download_image not fully implemented yet");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t as608_set_sys_param(uint8_t param_num, uint8_t value)
{
    uint8_t cmd[] = {
        AS608_CMD_SET_SYS_PARAM,
        param_num,
        value
    };
    uint8_t resp[8];
    size_t resp_len;

    esp_err_t ret = as608_send_cmd(cmd, sizeof(cmd), resp, &resp_len, s_config.timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    if (resp_len < 1) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t conf_code = resp[0];
    ESP_LOGD(TAG, "SetSysParam confirmation: 0x%02X", conf_code);

    if (conf_code != AS608_OK) {
        ESP_LOGW(TAG, "SetSysParam error: %s", as608_err_to_str(conf_code));
    }

    return as608_code_to_err(conf_code);
}

esp_err_t as608_led_control(uint8_t control, uint8_t speed, uint8_t color, uint8_t count)
{
    uint8_t cmd[] = {
        AS608_CMD_LED_CONFIG,
        control,
        speed,
        color,
        count
    };
    uint8_t resp[8];
    size_t resp_len;

    ESP_LOGI(TAG, "LED cmd: ctrl=%d speed=%d color=%d cnt=%d", control, speed, color, count);

    esp_err_t ret = as608_send_cmd(cmd, sizeof(cmd), resp, &resp_len, s_config.timeout_ms);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LED send failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (resp_len < 1) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t conf_code = resp[0];
    
    if (conf_code != AS608_OK) {
        ESP_LOGW(TAG, "LED not supported: code=0x%02X", conf_code);
    } else {
        ESP_LOGI(TAG, "LED OK");
    }

    return as608_code_to_err(conf_code);
}

esp_err_t as608_led_on_off(bool on)
{
    return as608_led_control(on ? AS608_LED_ON_ALWAYS : AS608_LED_OFF_ALWAYS, 
                             0, AS608_LED_BLUE, 0);
}

esp_err_t as608_read_index_table(uint8_t page, uint8_t *index_table)
{
    if (index_table == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t cmd[] = {
        AS608_CMD_READ_INDEX,
        page
    };
    uint8_t resp[64];
    size_t resp_len;

    esp_err_t ret = as608_send_cmd(cmd, sizeof(cmd), resp, &resp_len, s_config.timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    if (resp_len < 33) {  // 1 byte conf + 32 bytes index
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t conf_code = resp[0];
    
    if (conf_code == AS608_OK) {
        memcpy(index_table, &resp[1], 32);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "ReadIndex error: %s", as608_err_to_str(conf_code));
    return as608_code_to_err(conf_code);
}

esp_err_t as608_get_random(uint32_t *random_num)
{
    if (random_num == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t cmd[] = {AS608_CMD_GET_RANDOM};
    uint8_t resp[8];
    size_t resp_len;

    esp_err_t ret = as608_send_cmd(cmd, sizeof(cmd), resp, &resp_len, s_config.timeout_ms);
    if (ret != ESP_OK) {
        *random_num = 0;
        return ret;
    }

    if (resp_len < 5) {
        *random_num = 0;
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t conf_code = resp[0];
    
    if (conf_code == AS608_OK) {
        *random_num = ((uint32_t)resp[1] << 24) | ((uint32_t)resp[2] << 16) |
                      ((uint32_t)resp[3] << 8) | resp[4];
        ESP_LOGD(TAG, "Random number: 0x%08lX", (unsigned long)*random_num);
        return ESP_OK;
    }

    *random_num = 0;
    ESP_LOGW(TAG, "GetRandom error: %s", as608_err_to_str(conf_code));
    return as608_code_to_err(conf_code);
}

esp_err_t as608_set_password(uint32_t new_password)
{
    uint8_t cmd[] = {
        AS608_CMD_SET_PASSWORD,
        (new_password >> 24) & 0xFF,
        (new_password >> 16) & 0xFF,
        (new_password >> 8) & 0xFF,
        new_password & 0xFF
    };
    uint8_t resp[8];
    size_t resp_len;

    esp_err_t ret = as608_send_cmd(cmd, sizeof(cmd), resp, &resp_len, s_config.timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    if (resp_len < 1) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t conf_code = resp[0];
    ESP_LOGD(TAG, "SetPassword confirmation: 0x%02X", conf_code);

    if (conf_code == AS608_OK) {
        // Update stored password
        s_config.password = new_password;
        ESP_LOGI(TAG, "Password updated successfully");
    } else {
        ESP_LOGW(TAG, "SetPassword error: %s", as608_err_to_str(conf_code));
    }

    return as608_code_to_err(conf_code);
}

esp_err_t as608_set_address(uint32_t new_address)
{
    uint8_t cmd[] = {
        AS608_CMD_SET_ADDRESS,
        (new_address >> 24) & 0xFF,
        (new_address >> 16) & 0xFF,
        (new_address >> 8) & 0xFF,
        new_address & 0xFF
    };
    uint8_t resp[8];
    size_t resp_len;

    esp_err_t ret = as608_send_cmd(cmd, sizeof(cmd), resp, &resp_len, s_config.timeout_ms);
    if (ret != ESP_OK) {
        return ret;
    }

    if (resp_len < 1) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t conf_code = resp[0];
    ESP_LOGD(TAG, "SetAddress confirmation: 0x%02X", conf_code);

    if (conf_code == AS608_OK) {
        // Update stored address
        s_config.device_address = new_address;
        ESP_LOGI(TAG, "Device address updated successfully");
    } else {
        ESP_LOGW(TAG, "SetAddress error: %s", as608_err_to_str(conf_code));
    }

    return as608_code_to_err(conf_code);
}

const char* as608_err_to_str(uint8_t code)
{
    switch (code) {
        case AS608_OK:                  return "OK";
        case AS608_ERR_RECV_PKT:        return "Receive packet error";
        case AS608_ERR_NO_FINGER:       return "No finger detected";
        case AS608_ERR_ENROLL_FAIL:     return "Enroll failed";
        case AS608_ERR_IMAGE_MESSY:     return "Image too messy";
        case AS608_ERR_IMAGE_SMALL:     return "Image too small";
        case AS608_ERR_NO_MATCH:        return "No match";
        case AS608_ERR_NOT_FOUND:       return "Not found in library";
        case AS608_ERR_MERGE_FAIL:      return "Merge failed";
        case AS608_ERR_BAD_LOCATION:    return "Bad location";
        case AS608_ERR_READ_TEMPLATE:   return "Read template error";
        case AS608_ERR_UP_TEMPLATE:     return "Upload template error";
        case AS608_ERR_RECV_DATA:       return "Receive data error";
        case AS608_ERR_UP_IMAGE:        return "Upload image error";
        case AS608_ERR_DEL_TEMPLATE:    return "Delete template error";
        case AS608_ERR_EMPTY_LIB:       return "Empty library error";
        case AS608_ERR_INVALID_IMG:     return "Invalid image";
        case AS608_ERR_FLASH_RW:        return "Flash R/W error";
        case AS608_ERR_UNDEFINED:       return "Undefined error";
        case AS608_ERR_INVALID_REG:     return "Invalid register";
        case AS608_ERR_REG_CONFIG:      return "Register config error";
        case AS608_ERR_BAD_PKT:         return "Bad packet";
        default:                        return "Unknown error";
    }
}
