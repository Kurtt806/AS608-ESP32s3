#include "as608_protocol.h"
#include <string.h>

size_t as608_build_cmd_packet(uint8_t *buffer, uint8_t cmd, const uint8_t *params, size_t params_len) {
    /*
     * AS608 Packet Format (Big Endian):
     * [0-1]   Header: 0xEF 0x01
     * [2-5]   Address: 4 bytes (default 0xFFFFFFFF)
     * [6]     PID: 0x01 for command
     * [7-8]   Length: 2 bytes (instruction + params + checksum = 1 + params_len + 2)
     * [9]     Instruction code
     * [10..]  Parameters
     * [last2] Checksum: sum of PID + Length + Content
     */
    size_t idx = 0;
    
    /* Header */
    buffer[idx++] = AS608_HEADER_HIGH;  // 0xEF
    buffer[idx++] = AS608_HEADER_LOW;   // 0x01
    
    /* Address (Big Endian) */
    buffer[idx++] = 0xFF;
    buffer[idx++] = 0xFF;
    buffer[idx++] = 0xFF;
    buffer[idx++] = 0xFF;
    
    /* PID */
    buffer[idx++] = AS608_PID_COMMAND;  // 0x01
    
    /* Length = instruction(1) + params + checksum(2) */
    uint16_t length = 1 + params_len + 2;
    buffer[idx++] = (length >> 8) & 0xFF;   // Length high
    buffer[idx++] = length & 0xFF;          // Length low
    
    /* Instruction */
    buffer[idx++] = cmd;
    
    /* Parameters */
    if (params != NULL && params_len > 0) {
        memcpy(&buffer[idx], params, params_len);
        idx += params_len;
    }
    
    /* Checksum = PID + Length(2 bytes) + Instruction + Params */
    uint16_t checksum = AS608_PID_COMMAND;
    checksum += (length >> 8) & 0xFF;
    checksum += length & 0xFF;
    checksum += cmd;
    for (size_t i = 0; i < params_len; i++) {
        checksum += params[i];
    }
    buffer[idx++] = (checksum >> 8) & 0xFF;  // Checksum high
    buffer[idx++] = checksum & 0xFF;         // Checksum low
    
    return idx;
}

esp_err_t as608_parse_response(const uint8_t *buffer, size_t len, uint8_t *confirm, const uint8_t **data, size_t *data_len) {
    /*
     * Response Format:
     * [0-1]   Header: 0xEF 0x01
     * [2-5]   Address
     * [6]     PID: 0x07 for ACK
     * [7-8]   Length
     * [9]     Confirmation code
     * [10..]  Data (optional)
     * [last2] Checksum
     */
    if (len < 12) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    /* Check header */
    if (buffer[0] != AS608_HEADER_HIGH || buffer[1] != AS608_HEADER_LOW) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    /* Check PID */
    if (buffer[6] != AS608_PID_ACK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    /* Get length (Big Endian) */
    uint16_t length = (buffer[7] << 8) | buffer[8];
    
    /* Validate length */
    if (9 + length > len) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    /* Verify checksum */
    uint16_t checksum = 0;
    for (size_t i = 6; i < 9 + length - 2; i++) {
        checksum += buffer[i];
    }
    uint16_t recv_checksum = (buffer[9 + length - 2] << 8) | buffer[9 + length - 1];
    if (checksum != recv_checksum) {
        return ESP_ERR_INVALID_CRC;
    }
    
    /* Extract confirmation code */
    *confirm = buffer[9];
    
    /* Extract data */
    if (data != NULL && data_len != NULL) {
        *data = &buffer[10];
        *data_len = length - 3;  // Minus confirmation code(1) + checksum(2)
    }
    
    return ESP_OK;
}

const char *as608_confirm_str(uint8_t code) {
    switch (code) {
        case AS608_OK:              return "OK";
        case AS608_ERR_RECV_PKT:    return "RECV_PKT_ERR";
        case AS608_ERR_NO_FINGER:   return "NO_FINGER";
        case AS608_ERR_ENROLL_FAIL: return "ENROLL_FAIL";
        case AS608_ERR_IMG_DISORDER:return "IMG_DISORDER";
        case AS608_ERR_IMG_SMALL:   return "IMG_SMALL";
        case AS608_ERR_NO_MATCH:    return "NO_MATCH";
        case AS608_ERR_NOT_FOUND:   return "NOT_FOUND";
        case AS608_ERR_COMBINE_FAIL:return "COMBINE_FAIL";
        case AS608_ERR_BAD_LOCATION:return "BAD_LOCATION";
        case AS608_ERR_DB_READ_FAIL:return "DB_READ_FAIL";
        case AS608_ERR_DELETE_FAIL: return "DELETE_FAIL";
        case AS608_ERR_CLEAR_FAIL:  return "CLEAR_FAIL";
        case AS608_ERR_FLASH_ERR:   return "FLASH_ERR";
        default:                    return "UNKNOWN";
    }
}
