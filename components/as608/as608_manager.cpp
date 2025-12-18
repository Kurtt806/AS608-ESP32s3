#include "as608_manager.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "AS608";

AS608Manager& AS608Manager::GetInstance() {
    static AS608Manager instance;
    return instance;
}

esp_err_t AS608Manager::Initialize(const AS608Config& config) {
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing AS608 on UART %d, TX:%d, RX:%d, Baud:%lu",
             config.uart_num, config.tx_pin, config.rx_pin, config.baudrate);

    uart_port_ = config.uart_num;

    // Configure UART
    uart_config_t uart_config = {
        .baud_rate = (int)config.baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,  // Not used
        .source_clk = UART_SCLK_DEFAULT,
        .flags = 0,
    };

    esp_err_t ret = uart_param_config(uart_port_, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART param config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_set_pin(uart_port_, config.tx_pin, config.rx_pin,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART set pin failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_driver_install(uart_port_, 256, 256, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "AS608 initialized successfully");

    // Perform handshake
    return Handshake();
}

esp_err_t AS608Manager::Handshake() {
    if (!initialized_) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Performing handshake...");

    // PS_HandShake
    uint8_t cmd[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x03, 0x40, 0x00, 0x44};
    esp_err_t ret = SendCommand(cmd, sizeof(cmd));
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t response[12];
    size_t received_len;
    ret = ReceiveResponse(response, sizeof(response), &received_len);
    if (ret != ESP_OK) {
        return ret;
    }

    if (response[9] == 0x00) {
        ESP_LOGI(TAG, "Handshake successful");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Handshake failed, code: 0x%02X", response[9]);
        return ESP_FAIL;
    }
}

esp_err_t AS608Manager::TestConnection() {
    ESP_LOGI(TAG, "Testing UART connection...");
    
    // Send a simple test command
    uint8_t test_cmd[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x03, 0x40, 0x00, 0x44};
    esp_err_t ret = SendCommand(test_cmd, sizeof(test_cmd));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Test command send failed");
        return ret;
    }

    uint8_t response[12];
    size_t received_len;
    ret = ReceiveResponse(response, sizeof(response), &received_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Test response receive failed");
        return ret;
    }

    ESP_LOGI(TAG, "Test connection successful, received %d bytes", (int)received_len);
    return ESP_OK;
}

esp_err_t AS608Manager::ReadImage() {
    if (!initialized_) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Reading image...");

    // Simple command: PS_GetImage
    uint8_t cmd[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x03, 0x01, 0x00, 0x05};
    esp_err_t ret = SendCommand(cmd, sizeof(cmd));
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t response[12];
    size_t received_len;
    ret = ReceiveResponse(response, sizeof(response), &received_len);
    if (ret != ESP_OK) {
        return ret;
    }

    // Check confirmation code (position 9 in response)
    if (response[9] == 0x00) {
        ESP_LOGI(TAG, "Image read successfully");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Image read failed, code: 0x%02X", response[9]);
        ESP_LOG_BUFFER_HEX(TAG, response, received_len);
        if (response[9] == 0x02) {
            return ESP_ERR_NOT_FOUND;  // No finger
        } else {
            return ESP_FAIL;  // Other error
        }
    }
}

esp_err_t AS608Manager::GenerateCharacter(int buffer) {
    if (!initialized_) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Generating character file %d...", buffer);

    // PS_GenChar
    uint8_t cmd[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x04, 0x02, (uint8_t)buffer, 0x00, 0x08};
    esp_err_t ret = SendCommand(cmd, sizeof(cmd));
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t response[12];
    size_t received_len;
    ret = ReceiveResponse(response, sizeof(response), &received_len);
    if (ret != ESP_OK) {
        return ret;
    }

    if (response[9] == 0x00) {
        ESP_LOGI(TAG, "Character file %d generated successfully", buffer);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Generate character %d failed, code: 0x%02X", buffer, response[9]);
        return ESP_FAIL;
    }
}

esp_err_t AS608Manager::SearchTemplate(int* match_id, uint16_t* score) {
    if (!initialized_) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Searching template...");

    // PS_Search
    uint8_t cmd[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x08, 0x04, 0x01, 0x00, 0x00, 0x00, 0xA3, 0x00, 0xC8};
    esp_err_t ret = SendCommand(cmd, sizeof(cmd));
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t response[16];
    size_t received_len;
    ret = ReceiveResponse(response, sizeof(response), &received_len);
    if (ret != ESP_OK) {
        return ret;
    }

    if (response[9] == 0x00) {
        *match_id = (response[10] << 8) | response[11];
        *score = (response[12] << 8) | response[13];
        ESP_LOGI(TAG, "Template found, ID: %d, Score: %d", *match_id, *score);
        return ESP_OK;
    } else {
        ESP_LOGI(TAG, "Template not found, code: 0x%02X", response[9]);
        return ESP_ERR_NOT_FOUND;
    }
}

esp_err_t AS608Manager::GetTemplateCount(uint16_t* count) {
    if (!initialized_) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Getting template count...");

    // PS_TemplateNum
    uint8_t cmd[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x03, 0x1D, 0x00, 0x21};
    esp_err_t ret = SendCommand(cmd, sizeof(cmd));
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t response[14];
    size_t received_len;
    ret = ReceiveResponse(response, sizeof(response), &received_len);
    if (ret != ESP_OK) {
        return ret;
    }

    if (response[9] == 0x00) {
        *count = (response[10] << 8) | response[11];
        ESP_LOGI(TAG, "Template count: %d", *count);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Get template count failed, code: 0x%02X", response[9]);
        return ESP_FAIL;
    }
}

esp_err_t AS608Manager::RegisterModel() {
    if (!initialized_) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Registering model...");

    // PS_RegModel
    uint8_t cmd[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x03, 0x05, 0x00, 0x09};
    esp_err_t ret = SendCommand(cmd, sizeof(cmd));
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t response[12];
    size_t received_len;
    ret = ReceiveResponse(response, sizeof(response), &received_len);
    if (ret != ESP_OK) {
        return ret;
    }

    if (response[9] == 0x00) {
        ESP_LOGI(TAG, "Model registered successfully");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Register model failed, code: 0x%02X", response[9]);
        return ESP_FAIL;
    }
}

esp_err_t AS608Manager::StoreTemplate(int id) {
    if (!initialized_) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Storing template ID %d...", id);

    // PS_StoreChar
    uint8_t cmd[] = {0xEF, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x06, 0x06, 0x01, (uint8_t)(id >> 8), (uint8_t)(id & 0xFF), 0x00, 0x0E};
    esp_err_t ret = SendCommand(cmd, sizeof(cmd));
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t response[12];
    size_t received_len;
    ret = ReceiveResponse(response, sizeof(response), &received_len);
    if (ret != ESP_OK) {
        return ret;
    }

    if (response[9] == 0x00) {
        ESP_LOGI(TAG, "Template stored successfully at ID %d", id);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Store template failed, code: 0x%02X", response[9]);
        return ESP_FAIL;
    }
}

esp_err_t AS608Manager::SendCommand(const uint8_t* cmd, size_t cmd_len) {
    int written = uart_write_bytes(uart_port_, cmd, cmd_len);
    if (written != (int)cmd_len) {
        ESP_LOGE(TAG, "Failed to write command, written: %d, expected: %d", written, (int)cmd_len);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Command sent: %d bytes", written);
    return ESP_OK;
}

esp_err_t AS608Manager::ReceiveResponse(uint8_t* buffer, size_t buffer_size, size_t* received_len) {
    // Wait for response with timeout
    int len = uart_read_bytes(uart_port_, buffer, buffer_size, pdMS_TO_TICKS(1000));
    if (len < 0) {
        ESP_LOGE(TAG, "UART read error");
        return ESP_FAIL;
    }

    *received_len = len;
    ESP_LOGD(TAG, "Response received: %d bytes", len);

    if (len == 0) {
        ESP_LOGW(TAG, "No response received");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}