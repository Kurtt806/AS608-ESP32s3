/**
 * @file button.h
 * @brief Button Manager API - C++ Singleton
 */

#ifndef BUTTON_H
#define BUTTON_H

#include "esp_err.h"
#include "driver/gpio.h"
#include <stdint.h>
#include <functional>
#include <memory>
#include <mutex>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Button IDs
 * ============================================================================ */
typedef enum {
    BTN_ID_BOOT = 0,
    BTN_ID_MAX
} button_id_t;

#ifdef __cplusplus
}
#endif

/* ============================================================================
 * Button Events
 * ============================================================================ */
enum class ButtonEvent {
    Click,
    DoubleClick,
    LongPress,
    PressDown,
    PressUp
};

/* ============================================================================
 * Button Manager Configuration
 * ============================================================================ */
struct ButtonManagerConfig {
    gpio_num_t boot_gpio = GPIO_NUM_8;
    uint32_t long_press_ms = 2000;
    uint32_t short_press_ms = 180;
};

/* ============================================================================
 * Button Manager Class
 * ============================================================================ */
class ButtonManager {
public:
    static ButtonManager& GetInstance();

    // ==================== Lifecycle ====================
    
    esp_err_t Initialize(const ButtonManagerConfig& config = ButtonManagerConfig{});
    void Deinitialize();
    bool IsInitialized() const;

    // ==================== State ====================
    
    bool IsPressed(button_id_t id) const;

    // ==================== Events ====================
    
    void SetEventCallback(std::function<void(ButtonEvent, button_id_t)> callback);

    ButtonManager(const ButtonManager&) = delete;
    ButtonManager& operator=(const ButtonManager&) = delete;

private:
    ButtonManager();
    ~ButtonManager();

    void NotifyEvent(ButtonEvent event, button_id_t id);
    esp_err_t createButton(button_id_t id, gpio_num_t gpio, bool active_low);

    ButtonManagerConfig config_;
    bool initialized_ = false;
    void* buttons_[BTN_ID_MAX] = {nullptr};  // iot_button_handle_t

    std::function<void(ButtonEvent, button_id_t)> event_callback_;
    mutable std::mutex mutex_;
};

#endif /* BUTTON_H */
