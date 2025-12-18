/*
 * AS608 Fingerprint Sensor Manager Header
 *
 * Thread Safety:
 * - All public methods are thread-safe (protected by internal mutex)
 *
 * Usage:
 *   auto& as608 = AS608Manager::GetInstance();
 *   AS608Config config = { .uart_num = UART_NUM_1, .tx_pin = 17, .rx_pin = 16, .baudrate = 57600 };
 *   as608.Initialize(config);
 *   as608.ReadImage();
 */

#ifndef AS608_MANAGER_H
#define AS608_MANAGER_H

#include <esp_err.h>
#include <stdint.h>
#include <mutex>
#include <functional>
#include "as608_sensor.h"

// AS608 Events
enum class AS608Event {
    FingerDetected,
    MatchFound,
    MatchNotFound,
    EnrollSuccess,
    EnrollFail,
    Error
};

// Configuration structure
struct AS608Config {
    int uart_num;
    int tx_pin;
    int rx_pin;
    int baudrate;
};

class AS608Manager {
public:
    // Singleton access
    static AS608Manager& GetInstance();

    // Prevent copying
    AS608Manager(const AS608Manager&) = delete;
    AS608Manager& operator=(const AS608Manager&) = delete;

    // Initialization
    esp_err_t Initialize(const AS608Config& config);
    void Deinitialize();
    bool IsInitialized() const;

    // Event callback
    void SetEventCallback(std::function<void(AS608Event, int)> callback);

    // Fingerprint operations

    // Fingerprint operations
    esp_err_t ReadImage();
    esp_err_t GenerateCharacter(int buffer_id);
    esp_err_t RegisterModel();
    esp_err_t StoreTemplate(int id);
    esp_err_t SearchTemplate(int* match_id, uint16_t* score = nullptr);
    esp_err_t DeleteTemplate(int id);
    esp_err_t EmptyLibrary();
    esp_err_t GetTemplateCount(uint16_t* count);

    // High-level operations
    esp_err_t EnrollFingerprint(int id);

private:
    AS608Manager();
    ~AS608Manager();

    // Internal methods
    esp_err_t ExecuteCommand(uint8_t cmd, const uint8_t* params, size_t params_len,
                             uint8_t* confirm, const uint8_t** data, size_t* data_len);
    esp_err_t Handshake();
    static const char* ConfirmString(uint8_t confirm);

    // Member variables
    mutable std::mutex mutex_;
    bool initialized_ = false;
    AS608Sensor sensor_;
    std::function<void(AS608Event, int)> event_callback_;
};

#endif // AS608_MANAGER_H