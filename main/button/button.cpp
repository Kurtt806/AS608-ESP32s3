/**
 * @file button.cpp
 * @brief Button Manager Implementation (C++ Singleton)
 */

#include "button.h"
#include "../common/config.h"

#include "esp_log.h"
#include "iot_button.h"
#include <cstring>

static const char *TAG = "BUTTON_MANAGER";

/* ============================================================================
 * ButtonManager Singleton
 * ============================================================================ */
ButtonManager& ButtonManager::GetInstance() {
    static ButtonManager instance;
    return instance;
}

/* ============================================================================
 * Private Constructor/Destructor
 * ============================================================================ */
ButtonManager::ButtonManager() {
    std::memset(buttons_, 0, sizeof(buttons_));
}

ButtonManager::~ButtonManager() {
    Deinitialize();
}

/* ============================================================================
 * Lifecycle
 * ============================================================================ */
esp_err_t ButtonManager::Initialize(const ButtonManagerConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    config_ = config;
    ESP_LOGI(TAG, "Initializing ButtonManager...");

    // Create BOOT button
    esp_err_t ret = createButton(BTN_ID_BOOT, config_.boot_gpio, true);
    if (ret != ESP_OK) {
        return ret;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "ButtonManager initialized");
    return ESP_OK;
}

void ButtonManager::Deinitialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return;
    }

    for (int i = 0; i < BTN_ID_MAX; i++) {
        if (buttons_[i]) {
            iot_button_delete(static_cast<button_handle_t>(buttons_[i]));
            buttons_[i] = nullptr;
        }
    }

    initialized_ = false;
    ESP_LOGI(TAG, "ButtonManager deinitialized");
}

bool ButtonManager::IsInitialized() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

/* ============================================================================
 * State
 * ============================================================================ */
bool ButtonManager::IsPressed(button_id_t id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_ || id >= BTN_ID_MAX || buttons_[id] == nullptr) {
        return false;
    }
    
    return iot_button_get_key_level(static_cast<button_handle_t>(buttons_[id])) == 0;  // Active low
}

/* ============================================================================
 * Events
 * ============================================================================ */
void ButtonManager::SetEventCallback(std::function<void(ButtonEvent, button_id_t)> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    event_callback_ = callback;
}

void ButtonManager::NotifyEvent(ButtonEvent event, button_id_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (event_callback_) {
        event_callback_(event, id);
    }
}

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */
esp_err_t ButtonManager::createButton(button_id_t id, gpio_num_t gpio, bool active_low) {
    if (gpio == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "Button %d: GPIO not configured", id);
        return ESP_OK;
    }

    button_config_t cfg = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = static_cast<uint16_t>(config_.long_press_ms),
        .short_press_time = static_cast<uint16_t>(config_.short_press_ms),
        .gpio_button_config = {
            .gpio_num = gpio,
            .active_level = static_cast<uint8_t>(active_low ? 0 : 1),
            .disable_pull = false,
        },
    };

    buttons_[id] = iot_button_create(&cfg);
    if (buttons_[id] == nullptr) {
        ESP_LOGE(TAG, "Button %d create failed", id);
        return ESP_FAIL;
    }

    // Register callbacks
    iot_button_register_cb(static_cast<button_handle_t>(buttons_[id]), BUTTON_SINGLE_CLICK, 
                          [](void *arg, void *usr_data) {
                              ButtonManager::GetInstance().NotifyEvent(ButtonEvent::Click, 
                                                                     static_cast<button_id_t>(reinterpret_cast<uintptr_t>(usr_data)));
                          }, reinterpret_cast<void*>(static_cast<uintptr_t>(id)));
    
    iot_button_register_cb(static_cast<button_handle_t>(buttons_[id]), BUTTON_DOUBLE_CLICK, 
                          [](void *arg, void *usr_data) {
                              ButtonManager::GetInstance().NotifyEvent(ButtonEvent::DoubleClick, 
                                                                     static_cast<button_id_t>(reinterpret_cast<uintptr_t>(usr_data)));
                          }, reinterpret_cast<void*>(static_cast<uintptr_t>(id)));
    
    iot_button_register_cb(static_cast<button_handle_t>(buttons_[id]), BUTTON_LONG_PRESS_START, 
                          [](void *arg, void *usr_data) {
                              ButtonManager::GetInstance().NotifyEvent(ButtonEvent::LongPress, 
                                                                     static_cast<button_id_t>(reinterpret_cast<uintptr_t>(usr_data)));
                          }, reinterpret_cast<void*>(static_cast<uintptr_t>(id)));

    ESP_LOGI(TAG, "Button %d: GPIO%d initialized", id, gpio);
    return ESP_OK;
}
