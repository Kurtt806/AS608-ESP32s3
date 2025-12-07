/**
 * @file as608_protocol.hpp
 * @brief AS608 Protocol Definitions and Parser
 * 
 * Contains command codes, confirmation codes, and packet building/parsing logic.
 */

#ifndef AS608_PROTOCOL_HPP
#define AS608_PROTOCOL_HPP

#include <cstdint>
#include <cstddef>
#include <esp_err.h>

namespace as608 {

/**
 * @brief Protocol constants
 */
struct Protocol {
    static constexpr uint8_t HEADER_HIGH = 0xEF;
    static constexpr uint8_t HEADER_LOW = 0x01;
    static constexpr uint32_t DEFAULT_ADDRESS = 0xFFFFFFFF;
    static constexpr size_t MAX_PACKET_SIZE = 256;
    static constexpr size_t MIN_RESPONSE_SIZE = 12;
    
    // Packet Identifiers
    static constexpr uint8_t PID_COMMAND = 0x01;
    static constexpr uint8_t PID_DATA = 0x02;
    static constexpr uint8_t PID_ACK = 0x07;
    static constexpr uint8_t PID_END_DATA = 0x08;
};

/**
 * @brief AS608 Command codes
 */
enum class Command : uint8_t {
    GetImage        = 0x01,
    GenChar         = 0x02,
    Match           = 0x03,
    Search          = 0x04,
    RegModel        = 0x05,
    StoreChar       = 0x06,
    LoadChar        = 0x07,
    UpChar          = 0x08,
    DownChar        = 0x09,
    UpImage         = 0x0A,
    DownImage       = 0x0B,
    DeleteChar      = 0x0C,
    Empty           = 0x0D,
    SetSysPara      = 0x0E,
    ReadSysPara     = 0x0F,
    SetPwd          = 0x12,
    VfyPwd          = 0x13,
    GetRandom       = 0x14,
    SetAddr         = 0x15,
    Handshake       = 0x17,
    WriteNotepad    = 0x18,
    ReadNotepad     = 0x19,
    HiSpeedSearch   = 0x1B,
    TemplateCount   = 0x1D,
    ReadIndex       = 0x1F,
    AuraControl     = 0x35,
    CheckSensor     = 0x36,
};

/**
 * @brief AS608 Confirmation/Error codes
 */
enum class ConfirmCode : uint8_t {
    Ok              = 0x00,
    ErrRecvPkt      = 0x01,
    ErrNoFinger     = 0x02,
    ErrEnrollFail   = 0x03,
    ErrImgDisorder  = 0x06,
    ErrImgSmall     = 0x07,
    ErrNoMatch      = 0x08,
    ErrNotFound     = 0x09,
    ErrCombineFail  = 0x0A,
    ErrBadLocation  = 0x0B,
    ErrDbReadFail   = 0x0C,
    ErrUploadFail   = 0x0D,
    ErrNoRecvPkt    = 0x0E,
    ErrUploadImgFail = 0x0F,
    ErrDeleteFail   = 0x10,
    ErrClearFail    = 0x11,
    ErrBadPassword  = 0x13,
    ErrInvalidImage = 0x15,
    ErrFlashErr     = 0x18,
    ErrInvalidReg   = 0x1A,
    ErrBadConfig    = 0x1B,
    ErrBadNotepad   = 0x1C,
    ErrCommFail     = 0x1D,
};

/**
 * @brief Parsed response data
 */
struct ResponseData {
    ConfirmCode confirmCode;
    const uint8_t* data;
    size_t dataLen;
    
    ResponseData() : confirmCode(ConfirmCode::Ok), data(nullptr), dataLen(0) {}
};

/**
 * @brief Protocol parser and packet builder class
 */
class AS608Protocol {
public:
    AS608Protocol() = default;
    ~AS608Protocol() = default;
    
    /**
     * @brief Build a command packet
     * @param buffer Output buffer (must be at least MAX_PACKET_SIZE bytes)
     * @param cmd Command to send
     * @param params Parameters data (optional)
     * @param paramsLen Length of parameters
     * @return Number of bytes written to buffer
     */
    size_t buildCommandPacket(uint8_t* buffer, Command cmd,
                              const uint8_t* params = nullptr, size_t paramsLen = 0) const;
    
    /**
     * @brief Parse a response packet
     * @param buffer Input buffer containing response
     * @param len Length of data in buffer
     * @param response Output parsed response
     * @return ESP_OK on success
     */
    esp_err_t parseResponse(const uint8_t* buffer, size_t len, ResponseData& response) const;
    
    /**
     * @brief Get human-readable string for confirmation code
     * @param code Confirmation code
     * @return String description
     */
    static const char* confirmCodeToString(ConfirmCode code);
    
    /**
     * @brief Get human-readable string for confirmation code (raw uint8_t)
     * @param code Confirmation code value
     * @return String description
     */
    static const char* confirmCodeToString(uint8_t code);

private:
    /**
     * @brief Calculate checksum for packet data
     */
    uint16_t calculateChecksum(const uint8_t* data, size_t start, size_t len) const;
};

} // namespace as608

#endif // AS608_PROTOCOL_HPP
