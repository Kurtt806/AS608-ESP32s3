/**
 * @file as608.h
 * @brief AS608 Fingerprint Sensor Driver API
 * 
 * ESP-IDF component for AS608 optical fingerprint sensor
 * Supports: enrollment, search, delete, and system parameter read
 */

#ifndef __AS608_H__
#define __AS608_H__

#include <stdint.h>
#include "esp_err.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * AS608 Default Configuration
 * ============================================================================ */
#define AS608_DEFAULT_BAUD_RATE     57600
#define AS608_DEFAULT_ADDRESS       0xFFFFFFFF
#define AS608_DEFAULT_PASSWORD      0x00000000
#define AS608_DEFAULT_TIMEOUT_MS    2000
#define AS608_DEFAULT_LIBRARY_SIZE  162

/* ============================================================================
 * AS608 Configuration Structure
 * ============================================================================ */

/**
 * @brief AS608 configuration structure
 */
typedef struct {
    uart_port_t uart_port;      /**< UART port number (UART_NUM_1, etc.) */
    gpio_num_t tx_gpio;         /**< TX GPIO pin */
    gpio_num_t rx_gpio;         /**< RX GPIO pin */
    gpio_num_t rst_gpio;        /**< Reset GPIO (GPIO_NUM_NC if not used) */
    gpio_num_t pwr_en_gpio;     /**< Power enable GPIO (GPIO_NUM_NC if not used) */
    uint32_t baud_rate;         /**< Baud rate (default: 57600) */
    uint32_t device_address;    /**< Device address (default: 0xFFFFFFFF) */
    uint32_t password;          /**< Device password (default: 0x00000000) */
    uint16_t library_size;      /**< Fingerprint library capacity */
    uint32_t timeout_ms;        /**< Command timeout in milliseconds */
} as608_config_t;

/**
 * @brief Default configuration macro
 */
#define AS608_CONFIG_DEFAULT() { \
    .uart_port = UART_NUM_1, \
    .tx_gpio = GPIO_NUM_12, \
    .rx_gpio = GPIO_NUM_13, \
    .rst_gpio = GPIO_NUM_NC, \
    .pwr_en_gpio = GPIO_NUM_NC, \
    .baud_rate = AS608_DEFAULT_BAUD_RATE, \
    .device_address = AS608_DEFAULT_ADDRESS, \
    .password = AS608_DEFAULT_PASSWORD, \
    .library_size = AS608_DEFAULT_LIBRARY_SIZE, \
    .timeout_ms = AS608_DEFAULT_TIMEOUT_MS, \
}

/* ============================================================================
 * AS608 Packet Identifiers
 * ============================================================================ */
#define AS608_PID_COMMAND       0x01    /**< Command packet */
#define AS608_PID_DATA          0x02    /**< Data packet */
#define AS608_PID_ACK           0x07    /**< Acknowledge packet */
#define AS608_PID_END_DATA      0x08    /**< End of data packet */

/* ============================================================================
 * AS608 Instruction Codes
 * ============================================================================ */
#define AS608_CMD_GET_IMAGE         0x01    /**< Capture fingerprint image */
#define AS608_CMD_GEN_CHAR          0x02    /**< Generate character file from image */
#define AS608_CMD_MATCH             0x03    /**< Match two character files */
#define AS608_CMD_SEARCH            0x04    /**< Search fingerprint library */
#define AS608_CMD_REG_MODEL         0x05    /**< Generate template from character files */
#define AS608_CMD_STORE             0x06    /**< Store template to library */
#define AS608_CMD_LOAD_CHAR         0x07    /**< Load template from library */
#define AS608_CMD_UP_CHAR           0x08    /**< Upload character file to host */
#define AS608_CMD_DOWN_CHAR         0x09    /**< Download character file from host */
#define AS608_CMD_UP_IMAGE          0x0A    /**< Upload image to host */
#define AS608_CMD_DOWN_IMAGE        0x0B    /**< Download image from host */
#define AS608_CMD_DELETE_CHAR       0x0C    /**< Delete template from library */
#define AS608_CMD_EMPTY             0x0D    /**< Empty fingerprint library */
#define AS608_CMD_SET_SYS_PARAM     0x0E    /**< Set system parameter */
#define AS608_CMD_READ_SYS_PARAM    0x0F    /**< Read system parameters */
#define AS608_CMD_SET_PASSWORD      0x12    /**< Set device password */
#define AS608_CMD_VERIFY_PASSWORD   0x13    /**< Verify device password */
#define AS608_CMD_GET_RANDOM        0x14    /**< Get random number */
#define AS608_CMD_SET_ADDRESS       0x15    /**< Set device address */
#define AS608_CMD_READ_INFO_PAGE    0x16    /**< Read information page */
#define AS608_CMD_HANDSHAKE         0x17    /**< Handshake command */
#define AS608_CMD_TEMPLATE_COUNT    0x1D    /**< Get valid template count */
#define AS608_CMD_READ_INDEX        0x1F    /**< Read fingerprint template index table */
#define AS608_CMD_LED_CONFIG        0x35    /**< Aura LED control */
#define AS608_CMD_SOFT_RESET        0x3D    /**< Soft reset */

/* ============================================================================
 * AS608 LED Control Codes (for sensors with Aura LED)
 * ============================================================================ */
#define AS608_LED_BREATHING         0x01    /**< LED breathing light */
#define AS608_LED_FLASHING          0x02    /**< LED flashing */
#define AS608_LED_ON_ALWAYS         0x03    /**< LED always on */
#define AS608_LED_OFF_ALWAYS        0x04    /**< LED always off */
#define AS608_LED_GRADUAL_ON        0x05    /**< Gradual on then off */
#define AS608_LED_GRADUAL_OFF       0x06    /**< Gradual off then on */

/* LED color indexes */
#define AS608_LED_RED               0x01    /**< Red LED */
#define AS608_LED_BLUE              0x02    /**< Blue LED */
#define AS608_LED_PURPLE            0x03    /**< Purple LED */

/* ============================================================================
 * AS608 Packet Size Codes
 * ============================================================================ */
#define AS608_PACKET_SIZE_32        0x00    /**< 32 bytes data packet */
#define AS608_PACKET_SIZE_64        0x01    /**< 64 bytes data packet */
#define AS608_PACKET_SIZE_128       0x02    /**< 128 bytes data packet */
#define AS608_PACKET_SIZE_256       0x03    /**< 256 bytes data packet */

/* ============================================================================
 * AS608 Confirmation Codes (Response)
 * ============================================================================ */
#define AS608_OK                    0x00    /**< Command executed successfully */
#define AS608_ERR_RECV_PKT          0x01    /**< Error receiving data packet */
#define AS608_ERR_NO_FINGER         0x02    /**< No finger detected */
#define AS608_ERR_ENROLL_FAIL       0x03    /**< Failed to enroll finger */
#define AS608_ERR_IMAGE_MESSY       0x06    /**< Image too messy */
#define AS608_ERR_IMAGE_SMALL       0x07    /**< Image too small */
#define AS608_ERR_NO_MATCH          0x08    /**< No match found */
#define AS608_ERR_NOT_FOUND         0x09    /**< Search: no match in library */
#define AS608_ERR_MERGE_FAIL        0x0A    /**< Failed to merge character files */
#define AS608_ERR_BAD_LOCATION      0x0B    /**< Page ID out of range */
#define AS608_ERR_READ_TEMPLATE     0x0C    /**< Error reading template from library */
#define AS608_ERR_UP_TEMPLATE       0x0D    /**< Error uploading template */
#define AS608_ERR_RECV_DATA         0x0E    /**< Module can't receive data */
#define AS608_ERR_UP_IMAGE          0x0F    /**< Error uploading image */
#define AS608_ERR_DEL_TEMPLATE      0x10    /**< Failed to delete template */
#define AS608_ERR_EMPTY_LIB         0x11    /**< Failed to clear library */
#define AS608_ERR_INVALID_IMG       0x15    /**< Invalid image */
#define AS608_ERR_FLASH_RW          0x18    /**< Flash read/write error */
#define AS608_ERR_UNDEFINED         0x19    /**< Undefined error */
#define AS608_ERR_INVALID_REG       0x1A    /**< Invalid register */
#define AS608_ERR_REG_CONFIG        0x1B    /**< Register configuration error */
#define AS608_ERR_BAD_PKT           0x1C    /**< Bad packet */
#define AS608_ERR_TIMEOUT           0xFF    /**< Timeout error */
#define AS608_ERR_BADPACKET         0xFE    /**< Bad packet error */

/* Additional error codes for extended functionality */
#define AS608_ERR_UPLOAD_FAIL       0x0D    /**< Failed to upload character file */
#define AS608_ERR_RECV_DATA_FAIL    0x0E    /**< Module can't receive subsequent data */
#define AS608_ERR_UPLOAD_IMG_FAIL   0x0F    /**< Failed to upload image */

/* ============================================================================
 * AS608 System Parameters Structure
 * ============================================================================ */

/**
 * @brief AS608 system parameters structure
 */
typedef struct {
    uint16_t status_reg;        /**< Status register */
    uint16_t system_id;         /**< System identifier code */
    uint16_t library_size;      /**< Fingerprint library size */
    uint16_t security_level;    /**< Security level (1-5) */
    uint32_t device_address;    /**< Device address */
    uint16_t packet_size;       /**< Data packet size code */
    uint16_t baud_setting;      /**< Baud rate setting (N * 9600) */
} as608_sys_param_t;

/**
 * @brief AS608 search result structure
 */
typedef struct {
    uint16_t page_id;           /**< Matched template page ID */
    uint16_t match_score;       /**< Match confidence score */
} as608_search_result_t;

/**
 * @brief AS608 match result structure
 */
typedef struct {
    uint16_t score;             /**< Match score between two buffers */
} as608_match_result_t;

/* ============================================================================
 * AS608 API Functions
 * ============================================================================ */

/**
 * @brief Initialize AS608 sensor and UART communication
 * @param config Pointer to configuration structure
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t as608_init(const as608_config_t *config);

/**
 * @brief Get current library size (max fingerprint ID)
 * @return Library size
 */
uint16_t as608_get_library_size(void);

/**
 * @brief Deinitialize AS608 sensor and release UART resources
 * @return ESP_OK on success
 */
esp_err_t as608_deinit(void);

/**
 * @brief Capture fingerprint image to image buffer
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no finger detected
 */
esp_err_t as608_get_image(void);

/**
 * @brief Generate character file from image and store in buffer
 * @param buf_id Buffer ID (1 or 2)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t as608_gen_char(uint8_t buf_id);

/**
 * @brief Merge character files in buffer 1&2 and generate template
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t as608_reg_model(void);

/**
 * @brief Store template from buffer to flash library
 * @param id Page ID (location) to store template (0 to library_size-1)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t as608_store(uint16_t id);

/**
 * @brief Search fingerprint library for matching template
 * @param start_id Starting page ID for search
 * @param count Number of templates to search
 * @param match_id Pointer to store matched page ID (-1 if not found)
 * @param match_score Pointer to store match score
 * @return ESP_OK on match found, ESP_ERR_NOT_FOUND if no match
 */
esp_err_t as608_search(uint16_t start_id, uint16_t count, int16_t *match_id, uint16_t *match_score);

/**
 * @brief Delete one or more templates from library
 * @param id Starting page ID to delete
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t as608_delete(uint16_t id);

/**
 * @brief Delete multiple templates from library
 * @param start_id Starting page ID
 * @param count Number of templates to delete
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t as608_delete_range(uint16_t start_id, uint16_t count);

/**
 * @brief Empty (clear) entire fingerprint library
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t as608_empty(void);

/**
 * @brief Read system parameters from AS608
 * @param param Pointer to structure to store parameters
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t as608_read_sys_param(as608_sys_param_t *param);

/**
 * @brief Get valid template count in library
 * @param count Pointer to store template count
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t as608_get_template_count(uint16_t *count);

/**
 * @brief Verify device password
 * @param password 4-byte password
 * @return ESP_OK if password correct, error code otherwise
 */
esp_err_t as608_verify_password(uint32_t password);

/**
 * @brief Perform handshake with AS608 sensor
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t as608_handshake(void);

/**
 * @brief Match two character files (from buffer 1 and 2)
 * @param match_score Pointer to store match score
 * @return ESP_OK if matched, ESP_ERR_NOT_FOUND if no match
 */
esp_err_t as608_match(uint16_t *match_score);

/**
 * @brief Load template from flash to character buffer
 * @param buf_id Buffer ID (1 or 2)
 * @param id Page ID to load from
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t as608_load_char(uint8_t buf_id, uint16_t id);

/**
 * @brief Upload character file from buffer to host
 * @param buf_id Buffer ID (1 or 2)
 * @param char_buffer Pointer to buffer to store character file (512 bytes)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t as608_upload_char(uint8_t buf_id, uint8_t *char_buffer);

/**
 * @brief Download character file from host to buffer
 * @param buf_id Buffer ID (1 or 2)
 * @param char_buffer Pointer to character file data (512 bytes)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t as608_download_char(uint8_t buf_id, const uint8_t *char_buffer);

/**
 * @brief Upload fingerprint image from sensor to host
 * @param img_buffer Pointer to buffer to store image data
 * @param buf_size Size of image buffer
 * @param actual_size Pointer to store actual image size
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t as608_upload_image(uint8_t *img_buffer, size_t buf_size, size_t *actual_size);

/**
 * @brief Download fingerprint image from host to sensor
 * @param img_buffer Pointer to image data
 * @param img_size Size of image data
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t as608_download_image(const uint8_t *img_buffer, size_t img_size);

/**
 * @brief Set system parameter
 * @param param_num Parameter number (4=timeout, 5=packet_size, 6=baud_rate)
 * @param value Parameter value
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t as608_set_sys_param(uint8_t param_num, uint8_t value);

/**
 * @brief Control Aura LED (if available)
 * @param control LED control mode (breathing, flashing, on, off, etc.)
 * @param speed Speed of effect (0-255, faster at lower values)
 * @param color Color index (red, blue, purple)
 * @param count Number of cycles (0=infinite)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t as608_led_control(uint8_t control, uint8_t speed, uint8_t color, uint8_t count);

/**
 * @brief Simple LED on/off control
 * @param on true to turn LED on, false to turn off
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t as608_led_on_off(bool on);

/**
 * @brief Read fingerprint template index table
 * @param page Starting page number (0-3 for 162 templates)
 * @param index_table Pointer to buffer for index table (32 bytes)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t as608_read_index_table(uint8_t page, uint8_t *index_table);

/**
 * @brief Get random number from sensor
 * @param random_num Pointer to store 32-bit random number
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t as608_get_random(uint32_t *random_num);

/**
 * @brief Set new password
 * @param new_password 4-byte new password
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t as608_set_password(uint32_t new_password);

/**
 * @brief Set new device address
 * @param new_address 4-byte new address
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t as608_set_address(uint32_t new_address);

/**
 * @brief Get confirmation code string description
 * @param code Confirmation code from AS608
 * @return String description of the code
 */
const char* as608_err_to_str(uint8_t code);

#ifdef __cplusplus
}
#endif

#endif /* __AS608_H__ */
