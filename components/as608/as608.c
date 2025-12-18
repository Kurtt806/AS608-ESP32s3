#include "as608.h"
#include "as608_protocol.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "AS608";

static int s_uart_port = -1;
static uint8_t s_tx_buf[AS608_MAX_PACKET_SIZE];
static uint8_t s_rx_buf[AS608_MAX_PACKET_SIZE];

#define AS608_TIMEOUT_MS    1000
#define AS608_INTER_BYTE_TIMEOUT_MS  100

static esp_err_t as608_send_cmd(uint8_t cmd, const uint8_t *params, size_t params_len) {
    size_t pkt_len = as608_build_cmd_packet(s_tx_buf, cmd, params, params_len);
    
    /* Debug log */
    ESP_LOGD(TAG, "TX [%d]: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X...",
             (int)pkt_len,
             s_tx_buf[0], s_tx_buf[1], s_tx_buf[2], s_tx_buf[3], s_tx_buf[4],
             s_tx_buf[5], s_tx_buf[6], s_tx_buf[7], s_tx_buf[8], s_tx_buf[9]);
    
    /* Flush RX buffer before sending */
    uart_flush_input(s_uart_port);
    
    int written = uart_write_bytes(s_uart_port, (const char *)s_tx_buf, pkt_len);
    if (written != (int)pkt_len) {
        ESP_LOGE(TAG, "UART write failed: %d/%d", written, (int)pkt_len);
        return ESP_FAIL;
    }
    
    /* Wait for TX complete */
    esp_err_t ret = uart_wait_tx_done(s_uart_port, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART TX timeout");
        return ret;
    }
    
    return ESP_OK;
}

static esp_err_t as608_recv_response(uint8_t *confirm, const uint8_t **data, size_t *data_len, int timeout_ms) {
    memset(s_rx_buf, 0, sizeof(s_rx_buf));
    
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
        int len = uart_read_bytes(s_uart_port, s_rx_buf + total_read, 
                                   sizeof(s_rx_buf) - total_read,
                                   pdMS_TO_TICKS(remaining_timeout > 100 ? 100 : remaining_timeout));
        if (len > 0) {
            total_read += len;
        }
    }
    
    /* Debug log */
    ESP_LOGD(TAG, "RX [%d]: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
             total_read,
             s_rx_buf[0], s_rx_buf[1], s_rx_buf[2], s_rx_buf[3], s_rx_buf[4], s_rx_buf[5],
             s_rx_buf[6], s_rx_buf[7], s_rx_buf[8], s_rx_buf[9], s_rx_buf[10], s_rx_buf[11]);
    
    /* Parse response */
    esp_err_t ret = as608_parse_response(s_rx_buf, total_read, confirm, data, data_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Parse failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGD(TAG, "Confirm: 0x%02X (%s)", *confirm, as608_confirm_str(*confirm));
    return ESP_OK;
}

static esp_err_t as608_execute(uint8_t cmd, const uint8_t *params, size_t params_len,
                               uint8_t *confirm, const uint8_t **data, size_t *data_len) {
    esp_err_t ret = as608_send_cmd(cmd, params, params_len);
    if (ret != ESP_OK) {
        return ret;
    }
    
    return as608_recv_response(confirm, data, data_len, AS608_TIMEOUT_MS);
}

esp_err_t as608_init(const as608_config_t *cfg) {
    ESP_LOGI(TAG, "Init UART%d TX=%d RX=%d baud=%d", 
             cfg->uart_num, cfg->tx_pin, cfg->rx_pin, cfg->baudrate);
    
    uart_config_t uart_config = {
        .baud_rate = cfg->baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    esp_err_t ret = uart_param_config(cfg->uart_num, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = uart_set_pin(cfg->uart_num, cfg->tx_pin, cfg->rx_pin, 
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = uart_driver_install(cfg->uart_num, 1024, 0, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    s_uart_port = cfg->uart_num;
    
    /* Test connection with handshake */
    vTaskDelay(pdMS_TO_TICKS(200));  // Let sensor boot
    ret = as608_handshake();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Sensor connected");
    } else {
        ESP_LOGW(TAG, "Sensor handshake failed (may still work)");
    }
    
    return ESP_OK;
}

void as608_deinit(void) {
    if (s_uart_port >= 0) {
        uart_driver_delete(s_uart_port);
        s_uart_port = -1;
    }
}

esp_err_t as608_handshake(void) {
    uint8_t confirm;
    const uint8_t *data;
    size_t data_len;
    
    esp_err_t ret = as608_execute(AS608_CMD_HANDSHAKE, NULL, 0, &confirm, &data, &data_len);
    if (ret != ESP_OK) {
        return ret;
    }
    
    return (confirm == AS608_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t as608_read_image(void) {
    uint8_t confirm;
    const uint8_t *data;
    size_t data_len;
    
    esp_err_t ret = as608_execute(AS608_CMD_GET_IMAGE, NULL, 0, &confirm, &data, &data_len);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "read_image comm error");
        return ret;
    }
    
    if (confirm == AS608_ERR_NO_FINGER) {
        return ESP_ERR_NOT_FOUND;
    }
    
    if (confirm != AS608_OK) {
        ESP_LOGW(TAG, "read_image: %s (0x%02X)", as608_confirm_str(confirm), confirm);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Image captured");
    return ESP_OK;
}

esp_err_t as608_gen_char(int buffer_id) {
    uint8_t params[1] = { (uint8_t)buffer_id };
    uint8_t confirm;
    const uint8_t *data;
    size_t data_len;
    
    ESP_LOGI(TAG, "Generating char to buffer %d...", buffer_id);
    
    esp_err_t ret = as608_execute(AS608_CMD_GEN_CHAR, params, 1, &confirm, &data, &data_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gen_char(%d) execute failed: %s", buffer_id, esp_err_to_name(ret));
        return ret;
    }
    
    if (confirm != AS608_OK) {
        ESP_LOGW(TAG, "gen_char(%d): %s (0x%02X)", buffer_id, as608_confirm_str(confirm), confirm);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "gen_char(%d) OK - Feature extracted", buffer_id);
    return ESP_OK;
}

esp_err_t as608_reg_model(void) {
    uint8_t confirm;
    const uint8_t *data;
    size_t data_len;
    
    ESP_LOGI(TAG, "Combining CharBuffer1 + CharBuffer2...");
    
    esp_err_t ret = as608_execute(AS608_CMD_REG_MODEL, NULL, 0, &confirm, &data, &data_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "reg_model execute failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    if (confirm != AS608_OK) {
        ESP_LOGW(TAG, "reg_model: %s (0x%02X)", as608_confirm_str(confirm), confirm);
        if (confirm == 0x0A) {
            ESP_LOGE(TAG, "COMBINE_FAIL: The two fingerprints don't match!");
            ESP_LOGE(TAG, "Tip: Keep finger still, press firmly, same position both times");
        }
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "reg_model OK - Template created");
    return ESP_OK;
}

esp_err_t as608_store(int id) {
    /* Validate ID range: AS608 uses 0-based indexing, valid range is [0, capacity-1] */
    /* Most AS608 modules have 127 or 162 slots (0-126 or 0-161) */
    if (id < 0) {
        ESP_LOGE(TAG, "store: Invalid ID %d (must be >= 0)", id);
        return ESP_ERR_INVALID_ARG;
    }
    if (id > 200) {  /* Max reasonable capacity */
        ESP_LOGE(TAG, "store: ID %d exceeds maximum (200)", id);
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Storing template to ID %d...", id);
    
    /* 
     * AS608 Store command (0x06):
     * Params: BufferID(1 byte) + PageID(2 bytes, Big Endian)
     * BufferID: 1 = CharBuffer1, 2 = CharBuffer2
     * PageID: 0x0000 to 0x00FF (or more depending on capacity)
     */
    uint8_t params[3] = {
        0x01,                   /* Buffer 1 (template was created here by reg_model) */
        (uint8_t)((id >> 8) & 0xFF),  /* PageID high byte */
        (uint8_t)(id & 0xFF)          /* PageID low byte */
    };
    
    ESP_LOGD(TAG, "store params: BufferID=0x%02X, PageID=0x%02X%02X (=%d)", 
             params[0], params[1], params[2], id);
    
    uint8_t confirm;
    const uint8_t *data;
    size_t data_len;
    
    esp_err_t ret = as608_execute(AS608_CMD_STORE_CHAR, params, 3, &confirm, &data, &data_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "store execute failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    if (confirm != AS608_OK) {
        ESP_LOGE(TAG, "store: %s (0x%02X)", as608_confirm_str(confirm), confirm);
        if (confirm == AS608_ERR_BAD_LOCATION) {
            ESP_LOGE(TAG, "BAD_LOCATION: ID %d is outside valid range!", id);
            ESP_LOGE(TAG, "Hint: Check sensor capacity with ReadSysPara command");
        }
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, ">>> Template stored at ID %d <<<", id);
    return ESP_OK;
}

esp_err_t as608_search(int *match_id, uint16_t *score) {
    /* Params: BufferID(1) + StartPage(2) + PageNum(2) */
    uint8_t params[5] = {
        0x01,       // Buffer 1
        0x00, 0x00, // Start from 0
        0x00, 0xA3  // Search 163 entries (0x00A3)
    };
    uint8_t confirm;
    const uint8_t *data;
    size_t data_len;
    
    esp_err_t ret = as608_execute(AS608_CMD_SEARCH, params, 5, &confirm, &data, &data_len);
    if (ret != ESP_OK) {
        return ret;
    }
    
    if (confirm == AS608_ERR_NOT_FOUND || confirm == AS608_ERR_NO_MATCH) {
        *match_id = -1;
        if (score) *score = 0;
        return ESP_ERR_NOT_FOUND;
    }
    
    if (confirm != AS608_OK) {
        ESP_LOGW(TAG, "search: %s (0x%02X)", as608_confirm_str(confirm), confirm);
        return ESP_FAIL;
    }
    
    /* Response: PageID(2) + MatchScore(2) */
    if (data_len >= 4) {
        *match_id = (data[0] << 8) | data[1];
        if (score) *score = (data[2] << 8) | data[3];
        ESP_LOGI(TAG, "Match: ID=%d Score=%d", *match_id, score ? *score : 0);
    } else {
        *match_id = -1;
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t as608_delete(int id) {
    /* Params: PageID(2) + Count(2) */
    uint8_t params[4] = {
        (id >> 8) & 0xFF,
        id & 0xFF,
        0x00, 0x01  // Delete 1 template
    };
    uint8_t confirm;
    const uint8_t *data;
    size_t data_len;
    
    esp_err_t ret = as608_execute(AS608_CMD_DELETE_CHAR, params, 4, &confirm, &data, &data_len);
    if (ret != ESP_OK) {
        return ret;
    }
    
    if (confirm != AS608_OK) {
        ESP_LOGW(TAG, "delete: %s (0x%02X)", as608_confirm_str(confirm), confirm);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Deleted ID %d", id);
    return ESP_OK;
}

esp_err_t as608_empty(void) {
    uint8_t confirm;
    const uint8_t *data;
    size_t data_len;
    
    esp_err_t ret = as608_execute(AS608_CMD_EMPTY, NULL, 0, &confirm, &data, &data_len);
    if (ret != ESP_OK) {
        return ret;
    }
    
    if (confirm != AS608_OK) {
        ESP_LOGW(TAG, "empty: %s (0x%02X)", as608_confirm_str(confirm), confirm);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Database cleared");
    return ESP_OK;
}

esp_err_t as608_get_template_count(uint16_t *count) {
    uint8_t confirm;
    const uint8_t *data;
    size_t data_len;
    
    esp_err_t ret = as608_execute(AS608_CMD_TEMPLATE_COUNT, NULL, 0, &confirm, &data, &data_len);
    if (ret != ESP_OK) {
        return ret;
    }
    
    if (confirm != AS608_OK) {
        ESP_LOGW(TAG, "template_count: %s (0x%02X)", as608_confirm_str(confirm), confirm);
        return ESP_FAIL;
    }
    
    if (data_len >= 2) {
        *count = (data[0] << 8) | data[1];
        ESP_LOGD(TAG, "Template count: %d", *count);
    } else {
        *count = 0;
    }
    
    return ESP_OK;
}

esp_err_t as608_enroll(int id) {
    ESP_LOGI(TAG, "Starting enrollment process for ID %d", id);
    
    // Step 1: First image capture
    ESP_LOGI(TAG, "Step 1: Place finger on sensor for first scan...");
    esp_err_t ret = as608_read_image();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to capture first image: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Step 2: Generate char file 1
    ret = as608_gen_char(1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to generate char file 1: %s", esp_err_to_name(ret));
        return ret;
    }
    
    vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause
    
    // Step 3: Second image capture
    ESP_LOGI(TAG, "Step 2: Place same finger again for second scan...");
    ret = as608_read_image();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to capture second image: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Step 4: Generate char file 2
    ret = as608_gen_char(2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to generate char file 2: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Step 5: Combine char files into template
    ret = as608_reg_model();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create template: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Step 6: Store template
    ret = as608_store(id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store template: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Enrollment completed successfully for ID %d", id);
    return ESP_OK;
}
