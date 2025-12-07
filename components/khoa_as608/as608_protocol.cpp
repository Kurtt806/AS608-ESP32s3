/**
 * @file as608_protocol.cpp
 * @brief AS608 Protocol implementation
 */

#include "as608_protocol.hpp"
#include <cstring>

namespace as608 {

size_t AS608Protocol::buildCommandPacket(uint8_t* buffer, Command cmd,
                                         const uint8_t* params, size_t paramsLen) const {
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
    
    // Header
    buffer[idx++] = Protocol::HEADER_HIGH;
    buffer[idx++] = Protocol::HEADER_LOW;
    
    // Address (Big Endian)
    buffer[idx++] = 0xFF;
    buffer[idx++] = 0xFF;
    buffer[idx++] = 0xFF;
    buffer[idx++] = 0xFF;
    
    // PID
    buffer[idx++] = Protocol::PID_COMMAND;
    
    // Length = instruction(1) + params + checksum(2)
    uint16_t length = 1 + paramsLen + 2;
    buffer[idx++] = (length >> 8) & 0xFF;   // Length high
    buffer[idx++] = length & 0xFF;          // Length low
    
    // Instruction
    buffer[idx++] = static_cast<uint8_t>(cmd);
    
    // Parameters
    if (params != nullptr && paramsLen > 0) {
        std::memcpy(&buffer[idx], params, paramsLen);
        idx += paramsLen;
    }
    
    // Checksum = PID + Length(2 bytes) + Instruction + Params
    uint16_t checksum = Protocol::PID_COMMAND;
    checksum += (length >> 8) & 0xFF;
    checksum += length & 0xFF;
    checksum += static_cast<uint8_t>(cmd);
    for (size_t i = 0; i < paramsLen; i++) {
        checksum += params[i];
    }
    buffer[idx++] = (checksum >> 8) & 0xFF;  // Checksum high
    buffer[idx++] = checksum & 0xFF;         // Checksum low
    
    return idx;
}

esp_err_t AS608Protocol::parseResponse(const uint8_t* buffer, size_t len, ResponseData& response) const {
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
    if (len < Protocol::MIN_RESPONSE_SIZE) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Check header
    if (buffer[0] != Protocol::HEADER_HIGH || buffer[1] != Protocol::HEADER_LOW) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Check PID
    if (buffer[6] != Protocol::PID_ACK) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    
    // Get length (Big Endian)
    uint16_t length = (static_cast<uint16_t>(buffer[7]) << 8) | buffer[8];
    
    // Validate length
    if (9 + length > len) {
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Verify checksum
    uint16_t checksum = 0;
    for (size_t i = 6; i < 9 + length - 2; i++) {
        checksum += buffer[i];
    }
    uint16_t recvChecksum = (static_cast<uint16_t>(buffer[9 + length - 2]) << 8) 
                           | buffer[9 + length - 1];
    if (checksum != recvChecksum) {
        return ESP_ERR_INVALID_CRC;
    }
    
    // Extract confirmation code
    response.confirmCode = static_cast<ConfirmCode>(buffer[9]);
    
    // Extract data
    response.data = &buffer[10];
    response.dataLen = length - 3;  // Minus confirmation code(1) + checksum(2)
    
    return ESP_OK;
}

const char* AS608Protocol::confirmCodeToString(ConfirmCode code) {
    return confirmCodeToString(static_cast<uint8_t>(code));
}

const char* AS608Protocol::confirmCodeToString(uint8_t code) {
    switch (static_cast<ConfirmCode>(code)) {
        case ConfirmCode::Ok:              return "OK";
        case ConfirmCode::ErrRecvPkt:      return "RECV_PKT_ERR";
        case ConfirmCode::ErrNoFinger:     return "NO_FINGER";
        case ConfirmCode::ErrEnrollFail:   return "ENROLL_FAIL";
        case ConfirmCode::ErrImgDisorder:  return "IMG_DISORDER";
        case ConfirmCode::ErrImgSmall:     return "IMG_SMALL";
        case ConfirmCode::ErrNoMatch:      return "NO_MATCH";
        case ConfirmCode::ErrNotFound:     return "NOT_FOUND";
        case ConfirmCode::ErrCombineFail:  return "COMBINE_FAIL";
        case ConfirmCode::ErrBadLocation:  return "BAD_LOCATION";
        case ConfirmCode::ErrDbReadFail:   return "DB_READ_FAIL";
        case ConfirmCode::ErrDeleteFail:   return "DELETE_FAIL";
        case ConfirmCode::ErrClearFail:    return "CLEAR_FAIL";
        case ConfirmCode::ErrFlashErr:     return "FLASH_ERR";
        default:                           return "UNKNOWN";
    }
}

uint16_t AS608Protocol::calculateChecksum(const uint8_t* data, size_t start, size_t len) const {
    uint16_t sum = 0;
    for (size_t i = start; i < start + len; i++) {
        sum += data[i];
    }
    return sum;
}

} // namespace as608
