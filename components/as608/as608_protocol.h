#ifndef AS608_PROTOCOL_H
#define AS608_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <esp_err.h>

#define AS608_HEADER_HIGH       0xEF
#define AS608_HEADER_LOW        0x01
#define AS608_DEFAULT_ADDRESS   0xFFFFFFFF
#define AS608_MAX_PACKET_SIZE   256

/* Packet Identifier */
#define AS608_PID_COMMAND       0x01
#define AS608_PID_DATA          0x02
#define AS608_PID_ACK           0x07
#define AS608_PID_END_DATA      0x08

/* Instruction Codes */
typedef enum {
    AS608_CMD_GET_IMAGE         = 0x01,
    AS608_CMD_GEN_CHAR          = 0x02,
    AS608_CMD_MATCH             = 0x03,
    AS608_CMD_SEARCH            = 0x04,
    AS608_CMD_REG_MODEL         = 0x05,
    AS608_CMD_STORE_CHAR        = 0x06,
    AS608_CMD_LOAD_CHAR         = 0x07,
    AS608_CMD_UP_CHAR           = 0x08,
    AS608_CMD_DOWN_CHAR         = 0x09,
    AS608_CMD_UP_IMAGE          = 0x0A,
    AS608_CMD_DOWN_IMAGE        = 0x0B,
    AS608_CMD_DELETE_CHAR       = 0x0C,
    AS608_CMD_EMPTY             = 0x0D,
    AS608_CMD_SET_SYSPARA       = 0x0E,
    AS608_CMD_READ_SYSPARA      = 0x0F,
    AS608_CMD_SET_PWD           = 0x12,
    AS608_CMD_VFY_PWD           = 0x13,
    AS608_CMD_GET_RANDOM        = 0x14,
    AS608_CMD_SET_ADDR          = 0x15,
    AS608_CMD_HANDSHAKE         = 0x17,
    AS608_CMD_WRITE_NOTEPAD     = 0x18,
    AS608_CMD_READ_NOTEPAD      = 0x19,
    AS608_CMD_HISPEED_SEARCH    = 0x1B,
    AS608_CMD_TEMPLATE_COUNT    = 0x1D,
    AS608_CMD_READ_INDEX        = 0x1F,
    AS608_CMD_AURA_CONTROL      = 0x35,
    AS608_CMD_CHECK_SENSOR      = 0x36,
} as608_cmd_t;

/* Confirmation Codes */
typedef enum {
    AS608_OK                    = 0x00,
    AS608_ERR_RECV_PKT          = 0x01,
    AS608_ERR_NO_FINGER         = 0x02,
    AS608_ERR_ENROLL_FAIL       = 0x03,
    AS608_ERR_IMG_DISORDER      = 0x06,
    AS608_ERR_IMG_SMALL         = 0x07,
    AS608_ERR_NO_MATCH          = 0x08,
    AS608_ERR_NOT_FOUND         = 0x09,
    AS608_ERR_COMBINE_FAIL      = 0x0A,
    AS608_ERR_BAD_LOCATION      = 0x0B,
    AS608_ERR_DB_READ_FAIL      = 0x0C,
    AS608_ERR_UPLOAD_FAIL       = 0x0D,
    AS608_ERR_NO_RECV_PKT       = 0x0E,
    AS608_ERR_UPLOAD_IMG_FAIL   = 0x0F,
    AS608_ERR_DELETE_FAIL       = 0x10,
    AS608_ERR_CLEAR_FAIL        = 0x11,
    AS608_ERR_BAD_PASSWORD      = 0x13,
    AS608_ERR_INVALID_IMAGE     = 0x15,
    AS608_ERR_FLASH_ERR         = 0x18,
    AS608_ERR_INVALID_REG       = 0x1A,
    AS608_ERR_BAD_CONFIG        = 0x1B,
    AS608_ERR_BAD_NOTEPAD       = 0x1C,
    AS608_ERR_COMM_FAIL         = 0x1D,
} as608_confirm_t;

/**
 * @brief Build AS608 command packet (Big Endian format)
 */
size_t as608_build_cmd_packet(uint8_t *buffer, uint8_t cmd, const uint8_t *params, size_t params_len);

/**
 * @brief Parse AS608 response packet
 */
esp_err_t as608_parse_response(const uint8_t *buffer, size_t len, uint8_t *confirm, const uint8_t **data, size_t *data_len);

/**
 * @brief Get confirmation code string for debugging
 */
const char *as608_confirm_str(uint8_t code);

#endif // AS608_PROTOCOL_H
