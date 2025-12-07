/**
 * @file as608.hpp
 * @brief AS608 Fingerprint Sensor C++ API
 *
 * Object-oriented interface for AS608 fingerprint sensor with
 * event-driven callbacks and non-blocking state machines.
 */

#ifndef AS608_HPP
#define AS608_HPP

#include <cstdint>
#include <functional>
#include <esp_err.h>
#include <driver/uart.h>

namespace as608
{

    // Forward declarations
    class AS608Uart;
    class AS608Protocol;

    /**
     * @brief Event types for callback notifications
     */
    enum class Event
    {
        FingerDetected, ///< Finger placed on sensor
        FingerRemoved,  ///< Finger removed from sensor
        EnrollStart,    ///< Enrollment process started
        EnrollStep,     ///< Enrollment step completed (step number in data)
        EnrollComplete, ///< Enrollment completed successfully
        EnrollFailed,   ///< Enrollment failed
        MatchStart,     ///< Match process started
        MatchOk,        ///< Fingerprint matched (ID and score in data)
        MatchFailed,    ///< Fingerprint not matched
        Error,          ///< General error occurred
    };

    /**
     * @brief Data passed with events
     */
    struct EventData
    {
        int id;          ///< Fingerprint ID (for match/enroll)
        uint16_t score;  ///< Match score
        int step;        ///< Current step number
        int totalSteps;  ///< Total steps in process
        esp_err_t error; ///< Error code if applicable

        EventData() : id(-1), score(0), step(0), totalSteps(0), error(ESP_OK) {}
    };

    /**
     * @brief Callback function type
     */
    using EventCallback = std::function<void(Event event, const EventData &data)>;

    /**
     * @brief Sensor configuration
     */
    struct Config
    {
        uart_port_t uart_num;
        int tx_pin;
        int rx_pin;
        int baudrate;

        Config() : uart_num(UART_NUM_1), tx_pin(17), rx_pin(16), baudrate(57600) {}
        Config(uart_port_t num, int tx, int rx, int baud = 57600)
            : uart_num(num), tx_pin(tx), rx_pin(rx), baudrate(baud) {}
    };

    /**
     * @brief Enroll state machine states
     */
    enum class EnrollState
    {
        Idle,
        WaitFinger1,
        CaptureImage1,
        GenChar1,
        WaitRemoveFinger,
        WaitFinger2,
        CaptureImage2,
        GenChar2,
        CreateModel,
        StoreModel,
        Complete,
        Failed,
    };

    /**
     * @brief Match state machine states
     */
    enum class MatchState
    {
        Idle,
        WaitFinger,
        CaptureImage,
        GenChar,
        Search,
        Complete,
        Failed,
    };

    /**
     * @brief AS608 Fingerprint Sensor class
     *
     * Main class for interacting with AS608 fingerprint sensor.
     * Provides both synchronous (blocking) and asynchronous (non-blocking)
     * operations with event callbacks.
     */
    class AS608
    {
    public:
        static constexpr int DEFAULT_LIBRARY_SIZE = 163;
        static constexpr int ENROLL_TOTAL_STEPS = 6;

        AS608();
        ~AS608();

        // Non-copyable
        AS608(const AS608 &) = delete;
        AS608 &operator=(const AS608 &) = delete;

        //=========================================================================
        // Initialization
        //=========================================================================

        /**
         * @brief Initialize sensor with configuration
         * @param config Sensor configuration
         * @return ESP_OK on success
         */
        esp_err_t init(const Config &config);

        /**
         * @brief Deinitialize sensor
         */
        void deinit();

        /**
         * @brief Check if sensor is initialized
         */
        bool isInitialized() const { return m_initialized; }

        /**
         * @brief Set event callback
         * @param callback Callback function
         */
        void setCallback(EventCallback callback) { m_callback = callback; }

        //=========================================================================
        // Synchronous (Blocking) Operations
        //=========================================================================

        /**
         * @brief Handshake with sensor
         * @return ESP_OK if sensor responds
         */
        esp_err_t handshake();

        /**
         * @brief Read fingerprint image
         * @return ESP_OK if image captured, ESP_ERR_NOT_FOUND if no finger
         */
        esp_err_t readImage();

        /**
         * @brief Generate character file from image
         * @param bufferId Buffer ID (1 or 2)
         */
        esp_err_t genChar(int bufferId);

        /**
         * @brief Create model from two character buffers
         */
        esp_err_t regModel();

        /**
         * @brief Store template to flash
         * @param id Location ID (0-based)
         */
        esp_err_t store(int id);

        /**
         * @brief Search for fingerprint match
         * @param matchId Output matched ID (-1 if not found)
         * @param score Output match score
         * @return ESP_OK if found, ESP_ERR_NOT_FOUND if not
         */
        esp_err_t search(int *matchId, uint16_t *score);

        /**
         * @brief Delete template
         * @param id Template ID to delete
         */
        esp_err_t deleteTemplate(int id);

        /**
         * @brief Delete all templates
         */
        esp_err_t emptyLibrary();

        /**
         * @brief Get template count
         * @param count Output template count
         */
        esp_err_t getTemplateCount(uint16_t *count);

        //=========================================================================
        // Asynchronous (Non-Blocking) State Machines
        //=========================================================================

        /**
         * @brief Start enrollment process (non-blocking)
         * @param targetId ID to store the new fingerprint
         * @return ESP_OK if started successfully
         */
        esp_err_t startEnroll(int targetId);

        /**
         * @brief Cancel enrollment process
         */
        void cancelEnroll();

        /**
         * @brief Get current enrollment state
         */
        EnrollState getEnrollState() const { return m_enrollState; }

        /**
         * @brief Check if enrollment is in progress
         */
        bool isEnrolling() const { return m_enrollState != EnrollState::Idle; }

        /**
         * @brief Start match process (non-blocking)
         * @return ESP_OK if started successfully
         */
        esp_err_t startMatch();

        /**
         * @brief Cancel match process
         */
        void cancelMatch();

        /**
         * @brief Get current match state
         */
        MatchState getMatchState() const { return m_matchState; }

        /**
         * @brief Check if matching is in progress
         */
        bool isMatching() const { return m_matchState != MatchState::Idle; }

        /**
         * @brief Process state machines (call periodically in main loop)
         *
         * This method advances the enroll/match state machines.
         * Should be called frequently (e.g., every 50-100ms) when
         * an async operation is in progress.
         */
        void process();

    private:
        //=========================================================================
        // Private Members
        //=========================================================================

        bool m_initialized;
        EventCallback m_callback;

        // UART and Protocol handlers (PIMPL pattern for clean headers)
        AS608Uart *m_uart;
        AS608Protocol *m_protocol;

        // TX/RX buffers
        static constexpr size_t BUFFER_SIZE = 256;
        uint8_t m_txBuffer[BUFFER_SIZE];
        uint8_t m_rxBuffer[BUFFER_SIZE];

        // Enrollment state
        EnrollState m_enrollState;
        int m_enrollTargetId;
        int m_enrollStep;
        int m_enrollRetryCount;
        static constexpr int MAX_RETRY_COUNT = 3;

        // Match state
        MatchState m_matchState;
        int m_matchRetryCount;

        //=========================================================================
        // Private Methods
        //=========================================================================

        /**
         * @brief Execute a command and get response (blocking)
         */
        esp_err_t executeCommand(uint8_t cmd, const uint8_t *params, size_t paramsLen,
                                 uint8_t *confirmCode, const uint8_t **data, size_t *dataLen);

        /**
         * @brief Fire event callback
         */
        void fireEvent(Event event, const EventData &data = EventData());

        /**
         * @brief Process enrollment state machine
         */
        void processEnroll();

        /**
         * @brief Process match state machine
         */
        void processMatch();

        /**
         * @brief Reset enrollment state
         */
        void resetEnrollState();

        /**
         * @brief Reset match state
         */
        void resetMatchState();
    };

} // namespace as608

//=============================================================================
// C-compatible wrapper for ESP-IDF integration
//=============================================================================

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief C-compatible configuration structure
     */
    typedef struct
    {
        uart_port_t uart_num;
        int tx_pin;
        int rx_pin;
        int baudrate;
    } as608_config_t;

    /**
     * @brief C-compatible event types
     */
    typedef enum
    {
        AS608_EVENT_FINGER_DETECTED = 0,
        AS608_EVENT_FINGER_REMOVED,
        AS608_EVENT_ENROLL_START,
        AS608_EVENT_ENROLL_STEP,
        AS608_EVENT_ENROLL_COMPLETE,
        AS608_EVENT_ENROLL_FAILED,
        AS608_EVENT_MATCH_START,
        AS608_EVENT_MATCH_OK,
        AS608_EVENT_MATCH_FAILED,
        AS608_EVENT_ERROR,
    } as608_event_t;

    /**
     * @brief C-compatible event data
     */
    typedef struct
    {
        int id;
        uint16_t score;
        int step;
        int total_steps;
        esp_err_t error;
    } as608_event_data_t;

    /**
     * @brief C-compatible callback type
     */
    typedef void (*as608_callback_t)(as608_event_t event, const as608_event_data_t *data, void *user_data);

    /**
     * @brief Create AS608 instance
     * @return Opaque handle
     */
    void *as608_create(void);

    /**
     * @brief Destroy AS608 instance
     */
    void as608_destroy(void *handle);

    /**
     * @brief Initialize AS608
     */
    esp_err_t as608_init(void *handle, const as608_config_t *config);

    /**
     * @brief Deinitialize AS608
     */
    void as608_deinit(void *handle);

    /**
     * @brief Set callback with user data
     */
    void as608_set_callback(void *handle, as608_callback_t callback, void *user_data);

    /**
     * @brief Handshake
     */
    esp_err_t as608_handshake(void *handle);

    /**
     * @brief Read image
     */
    esp_err_t as608_read_image(void *handle);

    /**
     * @brief Generate character
     */
    esp_err_t as608_gen_char(void *handle, int buffer_id);

    /**
     * @brief Register model
     */
    esp_err_t as608_reg_model(void *handle);

    /**
     * @brief Store template
     */
    esp_err_t as608_store(void *handle, int id);

    /**
     * @brief Search fingerprint
     */
    esp_err_t as608_search(void *handle, int *match_id, uint16_t *score);

    /**
     * @brief Delete template
     */
    esp_err_t as608_delete(void *handle, int id);

    /**
     * @brief Empty library
     */
    esp_err_t as608_empty(void *handle);

    /**
     * @brief Get template count
     */
    esp_err_t as608_get_template_count(void *handle, uint16_t *count);

    /**
     * @brief Start enrollment (async)
     */
    esp_err_t as608_start_enroll(void *handle, int target_id);

    /**
     * @brief Cancel enrollment
     */
    void as608_cancel_enroll(void *handle);

    /**
     * @brief Start match (async)
     */
    esp_err_t as608_start_match(void *handle);

    /**
     * @brief Cancel match
     */
    void as608_cancel_match(void *handle);

    /**
     * @brief Process state machines
     */
    void as608_process(void *handle);

#ifdef __cplusplus
}
#endif

#endif // AS608_HPP
