/*
 * AS608 Fingerprint Sensor Manager Implementation
 */

#include "as608_manager.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

static const char *TAG = "AS608";

#define AS608_TIMEOUT_MS    1000
#define AS608_INTER_BYTE_TIMEOUT_MS  100

AS608Manager& AS608Manager::GetInstance() {
    static AS608Manager instance;
    return instance;
}

AS608Manager::AS608Manager() = default;

AS608Manager::~AS608Manager() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        Deinitialize();
    }
}

esp_err_t AS608Manager::Initialize(const AS608Config& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    esp_err_t ret = sensor_.Initialize(config.uart_num, config.tx_pin, config.rx_pin, config.baudrate);
    if (ret != ESP_OK) {
        return ret;
    }

    /* Test connection with handshake */
    vTaskDelay(pdMS_TO_TICKS(200));  // Let sensor boot
    ret = Handshake();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Sensor connected");
    } else {
        ESP_LOGW(TAG, "Sensor handshake failed (may still work)");
    }

    initialized_ = true;
    return ESP_OK;
}

void AS608Manager::Deinitialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    sensor_.Deinitialize();
    initialized_ = false;
}

bool AS608Manager::IsInitialized() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_ && sensor_.IsInitialized();
}

void AS608Manager::SetEventCallback(std::function<void(AS608Event, int)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    event_callback_ = callback;
}



esp_err_t AS608Manager::ExecuteCommand(uint8_t cmd, const uint8_t* params, size_t params_len,
                                       uint8_t* confirm, const uint8_t** data, size_t* data_len) {
    esp_err_t ret = sensor_.SendCommand(cmd, params, params_len);
    if (ret != ESP_OK) {
        return ret;
    }

    return sensor_.ReceiveResponse(confirm, data, data_len, AS608_TIMEOUT_MS);
}

esp_err_t AS608Manager::Handshake() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return ESP_ERR_INVALID_STATE;

    uint8_t confirm;
    const uint8_t *data;
    size_t data_len;

    esp_err_t ret = ExecuteCommand(AS608_CMD_HANDSHAKE, NULL, 0, &confirm, &data, &data_len);
    if (ret != ESP_OK) {
        return ret;
    }

    return (confirm == AS608_OK) ? ESP_OK : ESP_FAIL;
}

esp_err_t AS608Manager::ReadImage() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return ESP_ERR_INVALID_STATE;

    uint8_t confirm;
    const uint8_t *data;
    size_t data_len;

    esp_err_t ret = ExecuteCommand(AS608_CMD_GET_IMAGE, NULL, 0, &confirm, &data, &data_len);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "read_image comm error");
        return ret;
    }

    if (confirm == AS608_ERR_NO_FINGER) {
        return ESP_ERR_NOT_FOUND;
    }

    if (confirm != AS608_OK) {
        ESP_LOGW(TAG, "read_image: %s (0x%02X)", AS608Manager::ConfirmString(confirm), confirm);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Image captured");
    if (event_callback_) event_callback_(AS608Event::FingerDetected, 0);
    return ESP_OK;
}

esp_err_t AS608Manager::GenerateCharacter(int buffer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return ESP_ERR_INVALID_STATE;

    uint8_t params[1] = { (uint8_t)buffer_id };
    uint8_t confirm;
    const uint8_t *data;
    size_t data_len;

    ESP_LOGI(TAG, "Generating char to buffer %d...", buffer_id);

    esp_err_t ret = ExecuteCommand(AS608_CMD_GEN_CHAR, params, 1, &confirm, &data, &data_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gen_char(%d) execute failed: %s", buffer_id, esp_err_to_name(ret));
        return ret;
    }

    if (confirm != AS608_OK) {
        ESP_LOGW(TAG, "gen_char(%d): %s (0x%02X)", buffer_id, AS608Manager::ConfirmString(confirm), confirm);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "gen_char(%d) OK - Feature extracted", buffer_id);
    return ESP_OK;
}

esp_err_t AS608Manager::RegisterModel() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return ESP_ERR_INVALID_STATE;

    uint8_t confirm;
    const uint8_t *data;
    size_t data_len;

    ESP_LOGI(TAG, "Combining CharBuffer1 + CharBuffer2...");

    esp_err_t ret = ExecuteCommand(AS608_CMD_REG_MODEL, NULL, 0, &confirm, &data, &data_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "reg_model execute failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (confirm != AS608_OK) {
        ESP_LOGW(TAG, "reg_model: %s (0x%02X)", AS608Manager::ConfirmString(confirm), confirm);
        if (confirm == 0x0A) {
            ESP_LOGE(TAG, "COMBINE_FAIL: The two fingerprints don't match!");
            ESP_LOGE(TAG, "Tip: Keep finger still, press firmly, same position both times");
        }
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "reg_model OK - Template created");
    return ESP_OK;
}

esp_err_t AS608Manager::StoreTemplate(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return ESP_ERR_INVALID_STATE;

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

    esp_err_t ret = ExecuteCommand(AS608_CMD_STORE_CHAR, params, 3, &confirm, &data, &data_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "store execute failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (confirm != AS608_OK) {
        ESP_LOGE(TAG, "store: %s (0x%02X)", AS608Manager::ConfirmString(confirm), confirm);
        if (confirm == AS608_ERR_BAD_LOCATION) {
            ESP_LOGE(TAG, "BAD_LOCATION: ID %d is outside valid range!", id);
            ESP_LOGE(TAG, "Hint: Check sensor capacity with ReadSysPara command");
        }
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, ">>> Template stored at ID %d <<<", id);
    return ESP_OK;
}

esp_err_t AS608Manager::SearchTemplate(int* match_id, uint16_t* score) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return ESP_ERR_INVALID_STATE;

    /* Params: BufferID(1) + StartPage(2) + PageNum(2) */
    uint8_t params[5] = {
        0x01,       // Buffer 1
        0x00, 0x00, // Start from 0
        0x00, 0xA3  // Search 163 entries (0x00A3)
    };
    uint8_t confirm;
    const uint8_t *data;
    size_t data_len;

    esp_err_t ret = ExecuteCommand(AS608_CMD_SEARCH, params, 5, &confirm, &data, &data_len);
    if (ret != ESP_OK) {
        return ret;
    }

    if (confirm == AS608_ERR_NOT_FOUND || confirm == AS608_ERR_NO_MATCH) {
        *match_id = -1;
        if (score) *score = 0;
        if (event_callback_) event_callback_(AS608Event::MatchNotFound, 0);
        return ESP_ERR_NOT_FOUND;
    }

    if (confirm != AS608_OK) {
        ESP_LOGW(TAG, "search: %s (0x%02X)", AS608Manager::ConfirmString(confirm), confirm);
        if (event_callback_) event_callback_(AS608Event::Error, confirm);
        return ESP_FAIL;
    }

    /* Response: PageID(2) + MatchScore(2) */
    if (data_len >= 4) {
        *match_id = (data[0] << 8) | data[1];
        if (score) *score = (data[2] << 8) | data[3];
        ESP_LOGI(TAG, "Match: ID=%d Score=%d", *match_id, score ? *score : 0);
        if (event_callback_) event_callback_(AS608Event::MatchFound, *match_id);
    } else {
        *match_id = -1;
        if (event_callback_) event_callback_(AS608Event::Error, 0);
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t AS608Manager::DeleteTemplate(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return ESP_ERR_INVALID_STATE;

    /* Params: PageID(2) + Count(2) */
    uint8_t params[4] = {
        (uint8_t)((id >> 8) & 0xFF),
        (uint8_t)(id & 0xFF),
        0x00, 0x01  // Delete 1 template
    };
    uint8_t confirm;
    const uint8_t *data;
    size_t data_len;

    esp_err_t ret = ExecuteCommand(AS608_CMD_DELETE_CHAR, params, 4, &confirm, &data, &data_len);
    if (ret != ESP_OK) {
        return ret;
    }

    if (confirm != AS608_OK) {
        ESP_LOGW(TAG, "reg_model: %s (0x%02X)", AS608Manager::ConfirmString(confirm), confirm);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Deleted ID %d", id);
    return ESP_OK;
}

esp_err_t AS608Manager::EmptyLibrary() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return ESP_ERR_INVALID_STATE;

    uint8_t confirm;
    const uint8_t *data;
    size_t data_len;

    esp_err_t ret = ExecuteCommand(AS608_CMD_EMPTY, NULL, 0, &confirm, &data, &data_len);
    if (ret != ESP_OK) {
        return ret;
    }

    if (confirm != AS608_OK) {
        ESP_LOGW(TAG, "empty: %s (0x%02X)", AS608Manager::ConfirmString(confirm), confirm);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Database cleared");
    return ESP_OK;
}

esp_err_t AS608Manager::GetTemplateCount(uint16_t* count) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return ESP_ERR_INVALID_STATE;

    uint8_t confirm;
    const uint8_t *data;
    size_t data_len;

    esp_err_t ret = ExecuteCommand(AS608_CMD_TEMPLATE_COUNT, NULL, 0, &confirm, &data, &data_len);
    if (ret != ESP_OK) {
        return ret;
    }

    if (confirm != AS608_OK) {
        ESP_LOGW(TAG, "search: %s (0x%02X)", AS608Manager::ConfirmString(confirm), confirm);
        if (event_callback_) event_callback_(AS608Event::Error, confirm);
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

esp_err_t AS608Manager::EnrollFingerprint(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "Starting enrollment process for ID %d", id);

    // Step 1: First image capture
    ESP_LOGI(TAG, "Step 1: Place finger on sensor for first scan...");
    esp_err_t ret = ReadImage();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to capture first image: %s", esp_err_to_name(ret));
        return ret;
    }

    // Step 2: Generate char file 1
    ret = GenerateCharacter(1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to generate char file 1: %s", esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause

    // Step 3: Second image capture
    ESP_LOGI(TAG, "Step 2: Place same finger again for second scan...");
    ret = ReadImage();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to capture second image: %s", esp_err_to_name(ret));
        return ret;
    }

    // Step 4: Generate char file 2
    ret = GenerateCharacter(2);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to generate char file 2: %s", esp_err_to_name(ret));
        return ret;
    }

    // Step 5: Combine char files into template
    ret = RegisterModel();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create template: %s", esp_err_to_name(ret));
        return ret;
    }

    // Step 6: Store template
    ret = StoreTemplate(id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to store template: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Enrollment completed successfully for ID %d", id);
    if (event_callback_) event_callback_(AS608Event::EnrollSuccess, id);
    return ESP_OK;
}

const char* AS608Manager::ConfirmString(uint8_t confirm) {
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