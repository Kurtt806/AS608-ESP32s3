/*
 * AS608 Sensor Low-Level Interface
 */

#ifndef AS608_SENSOR_H
#define AS608_SENSOR_H

#include <esp_err.h>
#include <stdint.h>
#include "driver/uart.h"

// AS608 Command Codes
#define AS608_CMD_HANDSHAKE         0x40
#define AS608_CMD_GET_IMAGE         0x01
#define AS608_CMD_GEN_CHAR          0x02
#define AS608_CMD_REG_MODEL         0x05
#define AS608_CMD_STORE_CHAR        0x06
#define AS608_CMD_SEARCH            0x04
#define AS608_CMD_DELETE_CHAR       0x0C
#define AS608_CMD_EMPTY             0x0D
#define AS608_CMD_TEMPLATE_COUNT    0x1D

// AS608 Confirm Codes
#define AS608_OK                    0x00
#define AS608_ERR_NO_FINGER         0x02
#define AS608_ERR_NOT_FOUND         0x09
#define AS608_ERR_NO_MATCH          0x08
#define AS608_ERR_BAD_LOCATION      0x0B

class AS608Sensor {
public:
    AS608Sensor();
    ~AS608Sensor();

    esp_err_t Initialize(int uart_num, int tx_pin, int rx_pin, int baudrate);
    void Deinitialize();
    bool IsInitialized() const;

    esp_err_t SendCommand(uint8_t cmd, const uint8_t* params, size_t params_len);
    esp_err_t ReceiveResponse(uint8_t* confirm, const uint8_t** data, size_t* data_len, int timeout_ms = 1000);

private:
    size_t BuildCommandPacket(uint8_t* buf, uint8_t cmd, const uint8_t* params, size_t params_len);
    esp_err_t ParseResponse(const uint8_t* buf, size_t len, uint8_t* confirm, const uint8_t** data, size_t* data_len);
    const char* ConfirmString(uint8_t confirm);

    bool is_initialized_ = false;
    uart_port_t uart_port_;
    uint8_t tx_buf_[128];
    uint8_t rx_buf_[128];
};

#endif // AS608_SENSOR_H