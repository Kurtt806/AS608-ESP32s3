#pragma once

#include <cstdint>
#include "esp_err.h"
#include "driver/uart.h"
#include "driver/gpio.h"

struct AS608Config {
    uart_port_t uart_num;
    gpio_num_t tx_pin;
    gpio_num_t rx_pin;
    uint32_t baudrate;
};

class AS608Manager {
public:
    static AS608Manager& GetInstance();

    esp_err_t Initialize(const AS608Config& config);
    esp_err_t Handshake();
    esp_err_t TestConnection();
    esp_err_t ReadImage();
    esp_err_t GenerateCharacter(int buffer);
    esp_err_t SearchTemplate(int* match_id, uint16_t* score);
    esp_err_t GetTemplateCount(uint16_t* count);
    esp_err_t RegisterModel();
    esp_err_t StoreTemplate(int id);

private:
    AS608Manager() = default;
    ~AS608Manager() = default;
    AS608Manager(const AS608Manager&) = delete;
    AS608Manager& operator=(const AS608Manager&) = delete;

    // Simple UART communication
    esp_err_t SendCommand(const uint8_t* cmd, size_t cmd_len);
    esp_err_t ReceiveResponse(uint8_t* buffer, size_t buffer_size, size_t* received_len);

    uart_port_t uart_port_ = UART_NUM_MAX;
    bool initialized_ = false;
};