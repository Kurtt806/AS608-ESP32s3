/**
 * @file as608.cpp
 * @brief AS608 Fingerprint Sensor C++ Implementation
 */

#include "as608.hpp"
#include "as608_uart.hpp"
#include "as608_protocol.hpp"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>

namespace as608 {

static const char* TAG = "AS608";

//=============================================================================
// Constructor / Destructor
//=============================================================================

AS608::AS608()
    : m_initialized(false)
    , m_callback(nullptr)
    , m_uart(nullptr)
    , m_protocol(nullptr)
    , m_enrollState(EnrollState::Idle)
    , m_enrollTargetId(-1)
    , m_enrollStep(0)
    , m_enrollRetryCount(0)
    , m_matchState(MatchState::Idle)
    , m_matchRetryCount(0) {
    std::memset(m_txBuffer, 0, sizeof(m_txBuffer));
    std::memset(m_rxBuffer, 0, sizeof(m_rxBuffer));
    
    m_uart = new AS608Uart();
    m_protocol = new AS608Protocol();
}

AS608::~AS608() {
    deinit();
    delete m_uart;
    delete m_protocol;
}

//=============================================================================
// Initialization
//=============================================================================

esp_err_t AS608::init(const Config& config) {
    if (m_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }
    
    UartConfig uartConfig(config.uart_num, config.tx_pin, config.rx_pin, config.baudrate);
    esp_err_t ret = m_uart->init(uartConfig);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "UART init failed");
        return ret;
    }
    
    m_initialized = true;
    
    // Let sensor boot
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // Test connection
    ret = handshake();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Sensor connected");
    } else {
        ESP_LOGW(TAG, "Sensor handshake failed (may still work)");
    }
    
    return ESP_OK;
}

void AS608::deinit() {
    if (m_initialized) {
        cancelEnroll();
        cancelMatch();
        m_uart->deinit();
        m_initialized = false;
    }
}

//=============================================================================
// Event Handling
//=============================================================================

void AS608::fireEvent(Event event, const EventData& data) {
    if (m_callback) {
        m_callback(event, data);
    }
}

//=============================================================================
// Command Execution
//=============================================================================

esp_err_t AS608::executeCommand(uint8_t cmd, const uint8_t* params, size_t paramsLen,
                                 uint8_t* confirmCode, const uint8_t** data, size_t* dataLen) {
    if (!m_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Build command packet
    size_t pktLen = m_protocol->buildCommandPacket(m_txBuffer, static_cast<Command>(cmd), params, paramsLen);
    
    // Send command
    esp_err_t ret = m_uart->send(m_txBuffer, pktLen);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Receive response
    size_t bytesRead = 0;
    ret = m_uart->receive(m_rxBuffer, BUFFER_SIZE, &bytesRead, 1000);
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Parse response
    ResponseData response;
    ret = m_protocol->parseResponse(m_rxBuffer, bytesRead, response);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Parse failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    *confirmCode = static_cast<uint8_t>(response.confirmCode);
    if (data && dataLen) {
        *data = response.data;
        *dataLen = response.dataLen;
    }
    
    ESP_LOGD(TAG, "Confirm: 0x%02X (%s)", *confirmCode, 
             AS608Protocol::confirmCodeToString(*confirmCode));
    
    return ESP_OK;
}

//=============================================================================
// Synchronous Operations
//=============================================================================

esp_err_t AS608::handshake() {
    uint8_t confirm;
    const uint8_t* data;
    size_t dataLen;
    
    esp_err_t ret = executeCommand(static_cast<uint8_t>(Command::Handshake), 
                                    nullptr, 0, &confirm, &data, &dataLen);
    if (ret != ESP_OK) {
        return ret;
    }
    
    return (confirm == static_cast<uint8_t>(ConfirmCode::Ok)) ? ESP_OK : ESP_FAIL;
}

esp_err_t AS608::readImage() {
    uint8_t confirm;
    const uint8_t* data;
    size_t dataLen;
    
    esp_err_t ret = executeCommand(static_cast<uint8_t>(Command::GetImage),
                                    nullptr, 0, &confirm, &data, &dataLen);
    if (ret != ESP_OK) {
        return ret;
    }
    
    if (confirm == static_cast<uint8_t>(ConfirmCode::ErrNoFinger)) {
        return ESP_ERR_NOT_FOUND;
    }
    
    if (confirm != static_cast<uint8_t>(ConfirmCode::Ok)) {
        ESP_LOGW(TAG, "readImage: %s (0x%02X)", 
                 AS608Protocol::confirmCodeToString(confirm), confirm);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Image captured");
    return ESP_OK;
}

esp_err_t AS608::genChar(int bufferId) {
    uint8_t params[1] = { static_cast<uint8_t>(bufferId) };
    uint8_t confirm;
    const uint8_t* data;
    size_t dataLen;
    
    ESP_LOGI(TAG, "Generating char to buffer %d...", bufferId);
    
    esp_err_t ret = executeCommand(static_cast<uint8_t>(Command::GenChar),
                                    params, 1, &confirm, &data, &dataLen);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "genChar(%d) execute failed: %s", bufferId, esp_err_to_name(ret));
        return ret;
    }
    
    if (confirm != static_cast<uint8_t>(ConfirmCode::Ok)) {
        ESP_LOGW(TAG, "genChar(%d): %s (0x%02X)", bufferId,
                 AS608Protocol::confirmCodeToString(confirm), confirm);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "genChar(%d) OK - Feature extracted", bufferId);
    return ESP_OK;
}

esp_err_t AS608::regModel() {
    uint8_t confirm;
    const uint8_t* data;
    size_t dataLen;
    
    ESP_LOGI(TAG, "Combining CharBuffer1 + CharBuffer2...");
    
    esp_err_t ret = executeCommand(static_cast<uint8_t>(Command::RegModel),
                                    nullptr, 0, &confirm, &data, &dataLen);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "regModel execute failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    if (confirm != static_cast<uint8_t>(ConfirmCode::Ok)) {
        ESP_LOGW(TAG, "regModel: %s (0x%02X)",
                 AS608Protocol::confirmCodeToString(confirm), confirm);
        if (confirm == static_cast<uint8_t>(ConfirmCode::ErrCombineFail)) {
            ESP_LOGE(TAG, "COMBINE_FAIL: The two fingerprints don't match!");
        }
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "regModel OK - Template created");
    return ESP_OK;
}

esp_err_t AS608::store(int id) {
    if (id < 0 || id > 200) {
        ESP_LOGE(TAG, "store: Invalid ID %d", id);
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Storing template to ID %d...", id);
    
    uint8_t params[3] = {
        0x01,                               // Buffer 1
        static_cast<uint8_t>((id >> 8) & 0xFF),
        static_cast<uint8_t>(id & 0xFF)
    };
    
    uint8_t confirm;
    const uint8_t* data;
    size_t dataLen;
    
    esp_err_t ret = executeCommand(static_cast<uint8_t>(Command::StoreChar),
                                    params, 3, &confirm, &data, &dataLen);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "store execute failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    if (confirm != static_cast<uint8_t>(ConfirmCode::Ok)) {
        ESP_LOGE(TAG, "store: %s (0x%02X)",
                 AS608Protocol::confirmCodeToString(confirm), confirm);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, ">>> Template stored at ID %d <<<", id);
    return ESP_OK;
}

esp_err_t AS608::search(int* matchId, uint16_t* score) {
    uint8_t params[5] = {
        0x01,       // Buffer 1
        0x00, 0x00, // Start from 0
        0x00, 0xA3  // Search 163 entries
    };
    
    uint8_t confirm;
    const uint8_t* data;
    size_t dataLen;
    
    esp_err_t ret = executeCommand(static_cast<uint8_t>(Command::Search),
                                    params, 5, &confirm, &data, &dataLen);
    if (ret != ESP_OK) {
        return ret;
    }
    
    if (confirm == static_cast<uint8_t>(ConfirmCode::ErrNotFound) ||
        confirm == static_cast<uint8_t>(ConfirmCode::ErrNoMatch)) {
        *matchId = -1;
        if (score) *score = 0;
        return ESP_ERR_NOT_FOUND;
    }
    
    if (confirm != static_cast<uint8_t>(ConfirmCode::Ok)) {
        ESP_LOGW(TAG, "search: %s (0x%02X)",
                 AS608Protocol::confirmCodeToString(confirm), confirm);
        return ESP_FAIL;
    }
    
    if (dataLen >= 4) {
        *matchId = (static_cast<int>(data[0]) << 8) | data[1];
        if (score) *score = (static_cast<uint16_t>(data[2]) << 8) | data[3];
        ESP_LOGI(TAG, "Match: ID=%d Score=%d", *matchId, score ? *score : 0);
    } else {
        *matchId = -1;
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

esp_err_t AS608::deleteTemplate(int id) {
    uint8_t params[4] = {
        static_cast<uint8_t>((id >> 8) & 0xFF),
        static_cast<uint8_t>(id & 0xFF),
        0x00, 0x01  // Delete 1 template
    };
    
    uint8_t confirm;
    const uint8_t* data;
    size_t dataLen;
    
    esp_err_t ret = executeCommand(static_cast<uint8_t>(Command::DeleteChar),
                                    params, 4, &confirm, &data, &dataLen);
    if (ret != ESP_OK) {
        return ret;
    }
    
    if (confirm != static_cast<uint8_t>(ConfirmCode::Ok)) {
        ESP_LOGW(TAG, "delete: %s (0x%02X)",
                 AS608Protocol::confirmCodeToString(confirm), confirm);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Deleted ID %d", id);
    return ESP_OK;
}

esp_err_t AS608::emptyLibrary() {
    uint8_t confirm;
    const uint8_t* data;
    size_t dataLen;
    
    esp_err_t ret = executeCommand(static_cast<uint8_t>(Command::Empty),
                                    nullptr, 0, &confirm, &data, &dataLen);
    if (ret != ESP_OK) {
        return ret;
    }
    
    if (confirm != static_cast<uint8_t>(ConfirmCode::Ok)) {
        ESP_LOGW(TAG, "empty: %s (0x%02X)",
                 AS608Protocol::confirmCodeToString(confirm), confirm);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Database cleared");
    return ESP_OK;
}

esp_err_t AS608::getTemplateCount(uint16_t* count) {
    uint8_t confirm;
    const uint8_t* data;
    size_t dataLen;
    
    esp_err_t ret = executeCommand(static_cast<uint8_t>(Command::TemplateCount),
                                    nullptr, 0, &confirm, &data, &dataLen);
    if (ret != ESP_OK) {
        return ret;
    }
    
    if (confirm != static_cast<uint8_t>(ConfirmCode::Ok)) {
        ESP_LOGW(TAG, "template_count: %s (0x%02X)",
                 AS608Protocol::confirmCodeToString(confirm), confirm);
        return ESP_FAIL;
    }
    
    if (dataLen >= 2) {
        *count = (static_cast<uint16_t>(data[0]) << 8) | data[1];
        ESP_LOGD(TAG, "Template count: %d", *count);
    } else {
        *count = 0;
    }
    
    return ESP_OK;
}

//=============================================================================
// Asynchronous Enrollment State Machine
//=============================================================================

esp_err_t AS608::startEnroll(int targetId) {
    if (!m_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (m_enrollState != EnrollState::Idle) {
        ESP_LOGW(TAG, "Enrollment already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (m_matchState != MatchState::Idle) {
        ESP_LOGW(TAG, "Match in progress, cancel first");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (targetId < 0 || targetId > 200) {
        return ESP_ERR_INVALID_ARG;
    }
    
    m_enrollTargetId = targetId;
    m_enrollStep = 0;
    m_enrollRetryCount = 0;
    m_enrollState = EnrollState::WaitFinger1;
    
    EventData data;
    data.id = targetId;
    data.totalSteps = ENROLL_TOTAL_STEPS;
    fireEvent(Event::EnrollStart, data);
    
    ESP_LOGI(TAG, "Started enrollment for ID %d", targetId);
    return ESP_OK;
}

void AS608::cancelEnroll() {
    if (m_enrollState != EnrollState::Idle) {
        ESP_LOGI(TAG, "Enrollment cancelled");
        resetEnrollState();
    }
}

void AS608::resetEnrollState() {
    m_enrollState = EnrollState::Idle;
    m_enrollTargetId = -1;
    m_enrollStep = 0;
    m_enrollRetryCount = 0;
}

void AS608::processEnroll() {
    if (m_enrollState == EnrollState::Idle) {
        return;
    }
    
    EventData eventData;
    eventData.id = m_enrollTargetId;
    eventData.totalSteps = ENROLL_TOTAL_STEPS;
    
    esp_err_t ret;
    
    switch (m_enrollState) {
        case EnrollState::WaitFinger1:
            ret = readImage();
            if (ret == ESP_OK) {
                eventData.step = 1;
                fireEvent(Event::FingerDetected, eventData);
                m_enrollState = EnrollState::GenChar1;
            } else if (ret != ESP_ERR_NOT_FOUND) {
                m_enrollRetryCount++;
                if (m_enrollRetryCount >= MAX_RETRY_COUNT) {
                    eventData.error = ret;
                    fireEvent(Event::EnrollFailed, eventData);
                    resetEnrollState();
                }
            }
            break;
            
        case EnrollState::GenChar1:
            ret = genChar(1);
            if (ret == ESP_OK) {
                m_enrollStep = 1;
                eventData.step = 1;
                fireEvent(Event::EnrollStep, eventData);
                m_enrollState = EnrollState::WaitRemoveFinger;
                m_enrollRetryCount = 0;
            } else {
                m_enrollRetryCount++;
                if (m_enrollRetryCount >= MAX_RETRY_COUNT) {
                    eventData.error = ret;
                    fireEvent(Event::EnrollFailed, eventData);
                    resetEnrollState();
                } else {
                    m_enrollState = EnrollState::WaitFinger1;
                }
            }
            break;
            
        case EnrollState::WaitRemoveFinger:
            ret = readImage();
            if (ret == ESP_ERR_NOT_FOUND) {
                // Finger removed
                eventData.step = 2;
                fireEvent(Event::FingerRemoved, eventData);
                fireEvent(Event::EnrollStep, eventData);
                m_enrollStep = 2;
                m_enrollState = EnrollState::WaitFinger2;
                m_enrollRetryCount = 0;
            }
            // Keep waiting if finger still present
            break;
            
        case EnrollState::WaitFinger2:
            ret = readImage();
            if (ret == ESP_OK) {
                eventData.step = 3;
                fireEvent(Event::FingerDetected, eventData);
                m_enrollState = EnrollState::GenChar2;
            } else if (ret != ESP_ERR_NOT_FOUND) {
                m_enrollRetryCount++;
                if (m_enrollRetryCount >= MAX_RETRY_COUNT) {
                    eventData.error = ret;
                    fireEvent(Event::EnrollFailed, eventData);
                    resetEnrollState();
                }
            }
            break;
            
        case EnrollState::GenChar2:
            ret = genChar(2);
            if (ret == ESP_OK) {
                m_enrollStep = 3;
                eventData.step = 3;
                fireEvent(Event::EnrollStep, eventData);
                m_enrollState = EnrollState::CreateModel;
                m_enrollRetryCount = 0;
            } else {
                m_enrollRetryCount++;
                if (m_enrollRetryCount >= MAX_RETRY_COUNT) {
                    eventData.error = ret;
                    fireEvent(Event::EnrollFailed, eventData);
                    resetEnrollState();
                } else {
                    m_enrollState = EnrollState::WaitFinger2;
                }
            }
            break;
            
        case EnrollState::CreateModel:
            ret = regModel();
            if (ret == ESP_OK) {
                m_enrollStep = 4;
                eventData.step = 4;
                fireEvent(Event::EnrollStep, eventData);
                m_enrollState = EnrollState::StoreModel;
            } else {
                eventData.error = ret;
                fireEvent(Event::EnrollFailed, eventData);
                resetEnrollState();
            }
            break;
            
        case EnrollState::StoreModel:
            ret = store(m_enrollTargetId);
            if (ret == ESP_OK) {
                m_enrollStep = 5;
                eventData.step = 5;
                fireEvent(Event::EnrollStep, eventData);
                
                m_enrollStep = 6;
                eventData.step = 6;
                fireEvent(Event::EnrollComplete, eventData);
                resetEnrollState();
            } else {
                eventData.error = ret;
                fireEvent(Event::EnrollFailed, eventData);
                resetEnrollState();
            }
            break;
            
        default:
            break;
    }
}

//=============================================================================
// Asynchronous Match State Machine
//=============================================================================

esp_err_t AS608::startMatch() {
    if (!m_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (m_matchState != MatchState::Idle) {
        ESP_LOGW(TAG, "Match already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (m_enrollState != EnrollState::Idle) {
        ESP_LOGW(TAG, "Enrollment in progress, cancel first");
        return ESP_ERR_INVALID_STATE;
    }
    
    m_matchRetryCount = 0;
    m_matchState = MatchState::WaitFinger;
    
    fireEvent(Event::MatchStart);
    
    ESP_LOGI(TAG, "Started match process");
    return ESP_OK;
}

void AS608::cancelMatch() {
    if (m_matchState != MatchState::Idle) {
        ESP_LOGI(TAG, "Match cancelled");
        resetMatchState();
    }
}

void AS608::resetMatchState() {
    m_matchState = MatchState::Idle;
    m_matchRetryCount = 0;
}

void AS608::processMatch() {
    if (m_matchState == MatchState::Idle) {
        return;
    }
    
    EventData eventData;
    esp_err_t ret;
    
    switch (m_matchState) {
        case MatchState::WaitFinger:
            ret = readImage();
            if (ret == ESP_OK) {
                fireEvent(Event::FingerDetected, eventData);
                m_matchState = MatchState::GenChar;
            } else if (ret != ESP_ERR_NOT_FOUND) {
                m_matchRetryCount++;
                if (m_matchRetryCount >= MAX_RETRY_COUNT) {
                    eventData.error = ret;
                    fireEvent(Event::Error, eventData);
                    resetMatchState();
                }
            }
            break;
            
        case MatchState::GenChar:
            ret = genChar(1);
            if (ret == ESP_OK) {
                m_matchState = MatchState::Search;
                m_matchRetryCount = 0;
            } else {
                m_matchRetryCount++;
                if (m_matchRetryCount >= MAX_RETRY_COUNT) {
                    eventData.error = ret;
                    fireEvent(Event::MatchFailed, eventData);
                    resetMatchState();
                } else {
                    m_matchState = MatchState::WaitFinger;
                }
            }
            break;
            
        case MatchState::Search:
            {
                int matchId;
                uint16_t score;
                ret = search(&matchId, &score);
                
                if (ret == ESP_OK) {
                    eventData.id = matchId;
                    eventData.score = score;
                    fireEvent(Event::MatchOk, eventData);
                    resetMatchState();
                } else if (ret == ESP_ERR_NOT_FOUND) {
                    fireEvent(Event::MatchFailed, eventData);
                    resetMatchState();
                } else {
                    eventData.error = ret;
                    fireEvent(Event::Error, eventData);
                    resetMatchState();
                }
            }
            break;
            
        default:
            break;
    }
}

//=============================================================================
// Process Loop
//=============================================================================

void AS608::process() {
    processEnroll();
    processMatch();
}

} // namespace as608

//=============================================================================
// C-compatible Wrapper Implementation
//=============================================================================

struct AS608CWrapper {
    as608::AS608 instance;
    as608_callback_t callback;
    void* userData;
    
    AS608CWrapper() : callback(nullptr), userData(nullptr) {}
};

extern "C" {

void* as608_create(void) {
    return new AS608CWrapper();
}

void as608_destroy(void* handle) {
    if (handle) {
        delete static_cast<AS608CWrapper*>(handle);
    }
}

esp_err_t as608_init(void* handle, const as608_config_t* config) {
    if (!handle || !config) return ESP_ERR_INVALID_ARG;
    
    auto* wrapper = static_cast<AS608CWrapper*>(handle);
    as608::Config cfg(config->uart_num, config->tx_pin, config->rx_pin, config->baudrate);
    return wrapper->instance.init(cfg);
}

void as608_deinit(void* handle) {
    if (handle) {
        static_cast<AS608CWrapper*>(handle)->instance.deinit();
    }
}

void as608_set_callback(void* handle, as608_callback_t callback, void* user_data) {
    if (!handle) return;
    
    auto* wrapper = static_cast<AS608CWrapper*>(handle);
    wrapper->callback = callback;
    wrapper->userData = user_data;
    
    wrapper->instance.setCallback([wrapper](as608::Event event, const as608::EventData& data) {
        if (wrapper->callback) {
            as608_event_data_t cData;
            cData.id = data.id;
            cData.score = data.score;
            cData.step = data.step;
            cData.total_steps = data.totalSteps;
            cData.error = data.error;
            wrapper->callback(static_cast<as608_event_t>(event), &cData, wrapper->userData);
        }
    });
}

esp_err_t as608_handshake(void* handle) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    return static_cast<AS608CWrapper*>(handle)->instance.handshake();
}

esp_err_t as608_read_image(void* handle) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    return static_cast<AS608CWrapper*>(handle)->instance.readImage();
}

esp_err_t as608_gen_char(void* handle, int buffer_id) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    return static_cast<AS608CWrapper*>(handle)->instance.genChar(buffer_id);
}

esp_err_t as608_reg_model(void* handle) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    return static_cast<AS608CWrapper*>(handle)->instance.regModel();
}

esp_err_t as608_store(void* handle, int id) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    return static_cast<AS608CWrapper*>(handle)->instance.store(id);
}

esp_err_t as608_search(void* handle, int* match_id, uint16_t* score) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    return static_cast<AS608CWrapper*>(handle)->instance.search(match_id, score);
}

esp_err_t as608_delete(void* handle, int id) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    return static_cast<AS608CWrapper*>(handle)->instance.deleteTemplate(id);
}

esp_err_t as608_empty(void* handle) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    return static_cast<AS608CWrapper*>(handle)->instance.emptyLibrary();
}

esp_err_t as608_get_template_count(void* handle, uint16_t* count) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    return static_cast<AS608CWrapper*>(handle)->instance.getTemplateCount(count);
}

esp_err_t as608_start_enroll(void* handle, int target_id) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    return static_cast<AS608CWrapper*>(handle)->instance.startEnroll(target_id);
}

void as608_cancel_enroll(void* handle) {
    if (handle) {
        static_cast<AS608CWrapper*>(handle)->instance.cancelEnroll();
    }
}

esp_err_t as608_start_match(void* handle) {
    if (!handle) return ESP_ERR_INVALID_ARG;
    return static_cast<AS608CWrapper*>(handle)->instance.startMatch();
}

void as608_cancel_match(void* handle) {
    if (handle) {
        static_cast<AS608CWrapper*>(handle)->instance.cancelMatch();
    }
}

void as608_process(void* handle) {
    if (handle) {
        static_cast<AS608CWrapper*>(handle)->instance.process();
    }
}

} // extern "C"
