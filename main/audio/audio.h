/**
 * @file audio.h
 * @brief Audio Module API (MAX98357 I2S DAC)
 */

#ifndef AUDIO_H
#define AUDIO_H

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Sound Types
 * ============================================================================ */
typedef enum {
    SOUND_BOOT = 0,         /**< System boot */
    SOUND_READY,            /**< Ready/idle */
    SOUND_BEEP,             /**< Simple beep */
    SOUND_FINGER_DETECTED,  /**< Finger placed */
    SOUND_MATCH_OK,         /**< Fingerprint matched */
    SOUND_MATCH_FAIL,       /**< No match */
    SOUND_ENROLL_START,     /**< Enrollment started */
    SOUND_ENROLL_STEP,      /**< Enrollment step OK */
    SOUND_ENROLL_OK,        /**< Enrollment success */
    SOUND_ENROLL_FAIL,      /**< Enrollment failed */
    SOUND_DELETE_OK,        /**< Delete success */
    SOUND_ERROR,            /**< Error */
    SOUND_MAX
} sound_type_t;

/* ============================================================================
 * API
 * ============================================================================ */

/**
 * @brief Initialize audio module
 * @return ESP_OK on success
 */
esp_err_t audio_init(void);

/**
 * @brief Deinitialize audio module
 */
void audio_deinit(void);

/**
 * @brief Play a predefined sound
 * @param sound Sound type to play
 */
esp_err_t audio_play(sound_type_t sound);

/**
 * @brief Play a tone
 * @param freq_hz Frequency in Hz
 * @param duration_ms Duration in milliseconds
 */
esp_err_t audio_tone(uint16_t freq_hz, uint16_t duration_ms);

/**
 * @brief Stop current playback
 */
void audio_stop(void);

/**
 * @brief Set volume (0-100)
 */
void audio_set_volume(uint8_t volume);

/**
 * @brief Get current volume (0-100)
 */
uint8_t audio_get_volume(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_H */
