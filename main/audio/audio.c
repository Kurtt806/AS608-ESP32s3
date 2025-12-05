/**
 * @file audio.c
 * @brief Audio Module - Play embedded MP3 files via MAX98357 I2S DAC
 * 
 * Uses esp-libhelix-mp3 to decode MP3 files.
 * MP3 files are embedded into firmware binary.
 */

#include "audio.h"
#include "audio_events.h"
#include "../common/config.h"

#include "esp_log.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <math.h>

/* MP3 decoder from esp-libhelix-mp3 */
#include "mp3dec.h"

static const char *TAG = "AUDIO";

/* ============================================================================
 * Configuration
 * ============================================================================ */
#define I2S_BUFFER_SIZE     1024
#define TONE_SAMPLE_RATE    16000  /* For fallback tones only */

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

/* ============================================================================
 * Event Base Definition
 * ============================================================================ */
ESP_EVENT_DEFINE_BASE(AUDIO_EVENT);

/* ============================================================================
 * Embedded MP3 Files
 * ============================================================================ */

/* Macro to declare embedded files */
#define EMBED_MP3(name) \
    extern const uint8_t mp3_##name##_start[] asm("_binary_"#name"_mp3_start"); \
    extern const uint8_t mp3_##name##_end[] asm("_binary_"#name"_mp3_end");

/* Declare your MP3 files here */
EMBED_MP3(boot)
EMBED_MP3(match_ok)
EMBED_MP3(match_fail)
EMBED_MP3(enroll_start)
EMBED_MP3(enroll_step)
EMBED_MP3(enroll_ok)
EMBED_MP3(delete_ok)

/* Sound data structure */
typedef struct {
    const uint8_t *data;
    const uint8_t *end;
} sound_data_t;

#define SOUND_DATA(name) { mp3_##name##_start, mp3_##name##_end }
#define SOUND_EMPTY { NULL, NULL }

/* Sound mapping table */
static const sound_data_t SOUND_MAP[SOUND_MAX] = {
    [SOUND_BOOT] = SOUND_DATA(boot),
    [SOUND_READY] = SOUND_EMPTY,           /* Reuse boot sound */
    [SOUND_BEEP] = SOUND_EMPTY,            /* Use fallback tone */
    [SOUND_FINGER_DETECTED] = SOUND_EMPTY, /* Use fallback tone */
    [SOUND_MATCH_OK] = SOUND_DATA(match_ok),
    [SOUND_MATCH_FAIL] = SOUND_DATA(match_fail),
    [SOUND_ENROLL_START] = SOUND_DATA(enroll_start),
    [SOUND_ENROLL_STEP] = SOUND_DATA(enroll_step),
    [SOUND_ENROLL_OK] = SOUND_DATA(enroll_ok),    /* Reuse match_ok */
    [SOUND_ENROLL_FAIL] = SOUND_DATA(match_fail), /* Reuse match_fail */
    [SOUND_DELETE_OK] = SOUND_DATA(delete_ok),
    [SOUND_ERROR] = SOUND_DATA(match_fail),
};

/* ============================================================================
 * Fallback Tones
 * ============================================================================ */
typedef struct {
    uint16_t freq;
    uint16_t duration_ms;
} tone_t;

static const tone_t FALLBACK_BOOT[]       = {{880, 100}, {1760, 150}, {0, 0}};
static const tone_t FALLBACK_READY[]      = {{1000, 80}, {0, 0}};
static const tone_t FALLBACK_BEEP[]       = {{1500, 50}, {0, 0}};
static const tone_t FALLBACK_FINGER[]     = {{1200, 30}, {0, 0}};
static const tone_t FALLBACK_MATCH_OK[]   = {{1000, 80}, {1500, 80}, {2000, 120}, {0, 0}};
static const tone_t FALLBACK_MATCH_FAIL[] = {{400, 150}, {300, 200}, {0, 0}};
static const tone_t FALLBACK_ENR_START[]  = {{800, 100}, {1000, 100}, {0, 0}};
static const tone_t FALLBACK_ENR_STEP[]   = {{1200, 80}, {0, 0}};
static const tone_t FALLBACK_ENR_OK[]     = {{1000, 80}, {1200, 80}, {1500, 80}, {2000, 150}, {0, 0}};
static const tone_t FALLBACK_ENR_FAIL[]   = {{500, 100}, {400, 100}, {300, 200}, {0, 0}};
static const tone_t FALLBACK_DELETE[]     = {{1500, 50}, {1200, 50}, {900, 100}, {0, 0}};
static const tone_t FALLBACK_ERROR[]      = {{200, 300}, {0, 0}};

static const tone_t* FALLBACK_MAP[SOUND_MAX] = {
    [SOUND_BOOT]            = FALLBACK_BOOT,
    [SOUND_READY]           = FALLBACK_READY,
    [SOUND_BEEP]            = FALLBACK_BEEP,
    [SOUND_FINGER_DETECTED] = FALLBACK_FINGER,
    [SOUND_MATCH_OK]        = FALLBACK_MATCH_OK,
    [SOUND_MATCH_FAIL]      = FALLBACK_MATCH_FAIL,
    [SOUND_ENROLL_START]    = FALLBACK_ENR_START,
    [SOUND_ENROLL_STEP]     = FALLBACK_ENR_STEP,
    [SOUND_ENROLL_OK]       = FALLBACK_ENR_OK,
    [SOUND_ENROLL_FAIL]     = FALLBACK_ENR_FAIL,
    [SOUND_DELETE_OK]       = FALLBACK_DELETE,
    [SOUND_ERROR]           = FALLBACK_ERROR,
};

/* ============================================================================
 * Internal State
 * ============================================================================ */
typedef struct {
    sound_type_t sound;
} audio_cmd_t;

static i2s_chan_handle_t s_i2s_handle    = NULL;
static HMP3Decoder       s_mp3_decoder   = NULL;
static TaskHandle_t      s_task          = NULL;
static QueueHandle_t     s_queue         = NULL;
static uint8_t           s_volume        = 80;
static volatile bool     s_stop_flag     = false;
static int               s_current_rate  = 0;  /* Current I2S sample rate */

/* ============================================================================
 * Tone Generation (Fallback)
 * ============================================================================ */
static esp_err_t set_sample_rate(int sample_rate);

static void generate_tone(int16_t *buf, size_t samples, uint16_t freq, float vol) {
    static float phase = 0;
    float amp = 32767.0f * vol;
    float inc = 2.0f * M_PI * freq / TONE_SAMPLE_RATE;
    
    for (size_t i = 0; i < samples; i++) {
        buf[i] = (int16_t)(amp * sinf(phase));
        phase += inc;
        if (phase >= 2.0f * M_PI) phase -= 2.0f * M_PI;
    }
}

static void play_tone(uint16_t freq, uint16_t duration_ms) {
    if (!s_i2s_handle || freq == 0 || duration_ms == 0) return;
    
    /* Switch to tone sample rate */
    set_sample_rate(TONE_SAMPLE_RATE);
    
    int16_t buf[256];
    size_t total = (TONE_SAMPLE_RATE * duration_ms) / 1000;
    size_t done = 0;
    float vol = s_volume / 100.0f;
    size_t written;
    
    while (done < total && !s_stop_flag) {
        size_t n = (total - done < 256) ? (total - done) : 256;
        generate_tone(buf, n, freq, vol);
        i2s_channel_write(s_i2s_handle, buf, n * 2, &written, pdMS_TO_TICKS(100));
        done += n;
    }
}

static void play_fallback(const tone_t *tones) {
    if (!tones) return;
    for (int i = 0; tones[i].duration_ms && !s_stop_flag; i++) {
        play_tone(tones[i].freq, tones[i].duration_ms);
    }
}

/* ============================================================================
 * Dynamic Sample Rate
 * ============================================================================ */
static esp_err_t set_sample_rate(int sample_rate) {
    if (s_current_rate == sample_rate) return ESP_OK;
    if (!s_i2s_handle) return ESP_ERR_INVALID_STATE;
    
    ESP_LOGI(TAG, "Changing sample rate: %d -> %d Hz", s_current_rate, sample_rate);
    
    /* Disable channel, reconfigure clock, re-enable */
    i2s_channel_disable(s_i2s_handle);
    
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    esp_err_t ret = i2s_channel_reconfig_std_clock(s_i2s_handle, &clk_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set sample rate: %s", esp_err_to_name(ret));
        i2s_channel_enable(s_i2s_handle);
        return ret;
    }
    
    i2s_channel_enable(s_i2s_handle);
    s_current_rate = sample_rate;
    return ESP_OK;
}

/* ============================================================================
 * MP3 Playback
 * ============================================================================ */
static void play_mp3(const uint8_t *data, size_t size) {
    if (!s_i2s_handle || !s_mp3_decoder || !data || size < 10) return;
    
    ESP_LOGI(TAG, "Playing MP3: %d bytes", size);
    
    uint8_t *read_ptr = (uint8_t *)data;
    int bytes_left = size;
    int16_t output_buf[1152 * 2];  /* Max samples per MP3 frame * 2 channels */
    float vol = s_volume / 100.0f;
    size_t written;
    MP3FrameInfo frame_info;
    bool rate_set = false;
    
    s_stop_flag = false;
    
    while (bytes_left > 0 && !s_stop_flag) {
        /* Find sync word */
        int offset = MP3FindSyncWord(read_ptr, bytes_left);
        if (offset < 0) {
            break;  /* No more frames */
        }
        
        read_ptr += offset;
        bytes_left -= offset;
        
        if (bytes_left < 4) break;
        
        /* Decode frame */
        int err = MP3Decode(s_mp3_decoder, &read_ptr, &bytes_left, output_buf, 0);
        if (err != ERR_MP3_NONE) {
            if (err == ERR_MP3_INDATA_UNDERFLOW || err == ERR_MP3_MAINDATA_UNDERFLOW) {
                continue;  /* Need more data, try next frame */
            }
            ESP_LOGW(TAG, "MP3 decode error: %d", err);
            break;
        }
        
        /* Get frame info */
        MP3GetLastFrameInfo(s_mp3_decoder, &frame_info);
        
        /* Set I2S sample rate to match MP3 on first frame */
        if (!rate_set) {
            ESP_LOGI(TAG, "MP3 info: %d Hz, %d ch, %d kbps", 
                     frame_info.samprate, frame_info.nChans, frame_info.bitrate/1000);
            set_sample_rate(frame_info.samprate);
            rate_set = true;
        }
        
        int samples = frame_info.outputSamps;
        if (frame_info.nChans == 2) {
            samples /= 2;  /* outputSamps is total for both channels */
        }
        
        /* Convert to mono and apply volume */
        int16_t mono_buf[1152];
        for (int i = 0; i < samples; i++) {
            int32_t sample;
            if (frame_info.nChans == 2) {
                sample = ((int32_t)output_buf[i*2] + output_buf[i*2+1]) / 2;
            } else {
                sample = output_buf[i];
            }
            mono_buf[i] = (int16_t)(sample * vol);
        }
        
        /* Write to I2S */
        i2s_channel_write(s_i2s_handle, mono_buf, samples * sizeof(int16_t),
                          &written, pdMS_TO_TICKS(100));
    }
    
    ESP_LOGI(TAG, "MP3 playback done");
}

/* ============================================================================
 * Audio Task
 * ============================================================================ */
static void audio_task(void *arg) {
    (void)arg;
    audio_cmd_t cmd;
    
    ESP_LOGI(TAG, "Task started");
    
    while (1) {
        if (xQueueReceive(s_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            if (cmd.sound >= SOUND_MAX) continue;
            
            s_stop_flag = false;
            esp_event_post(AUDIO_EVENT, AUDIO_EVT_PLAY_START, NULL, 0, 0);
            
            const sound_data_t *snd = &SOUND_MAP[cmd.sound];
            size_t size = (snd->data && snd->end) ? (snd->end - snd->data) : 0;
            
            /* Play MP3 if available, else fallback to tone */
            if (size >= 10 && snd->data[0] == 0xFF && (snd->data[1] & 0xE0) == 0xE0) {
                /* Valid MP3 sync word */
                play_mp3(snd->data, size);
            } else if (size >= 10 && snd->data[0] == 'I' && snd->data[1] == 'D' && snd->data[2] == '3') {
                /* MP3 with ID3 tag */
                play_mp3(snd->data, size);
            } else {
                ESP_LOGD(TAG, "No MP3 for sound %d, using fallback", cmd.sound);
                play_fallback(FALLBACK_MAP[cmd.sound]);
            }
            
            esp_event_post(AUDIO_EVENT, AUDIO_EVT_PLAY_DONE, NULL, 0, 0);
        }
    }
}

/* ============================================================================
 * I2S Init
 * ============================================================================ */
static esp_err_t i2s_init(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_i2s_handle, NULL));
    
    /* Start with a default rate, will be changed dynamically */
    s_current_rate = CFG_AUDIO_SAMPLE_RATE;
    
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(s_current_rate),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CFG_I2S_BCK,
            .ws   = CFG_I2S_WS,
            .dout = CFG_I2S_DATA,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { false, false, false },
        },
    };
    
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_i2s_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_i2s_handle));
    
    ESP_LOGI(TAG, "I2S: BCK=%d, WS=%d, DATA=%d, rate=%d", 
             CFG_I2S_BCK, CFG_I2S_WS, CFG_I2S_DATA, s_current_rate);
    return ESP_OK;
}

/* ============================================================================
 * Public API
 * ============================================================================ */
esp_err_t audio_init(void) {
    if (s_i2s_handle) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing...");
    
    /* Init I2S */
    esp_err_t ret = i2s_init();
    if (ret != ESP_OK) return ret;
    
    /* Init MP3 decoder */
    s_mp3_decoder = MP3InitDecoder();
    if (!s_mp3_decoder) {
        ESP_LOGE(TAG, "MP3 decoder init failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "MP3 decoder initialized");
    
    /* Create queue */
    s_queue = xQueueCreate(CFG_AUDIO_QUEUE_SIZE, sizeof(audio_cmd_t));
    if (!s_queue) {
        ESP_LOGE(TAG, "Queue create failed");
        return ESP_ERR_NO_MEM;
    }
    
    /* Create task */
    if (xTaskCreate(audio_task, "audio", CFG_AUDIO_TASK_STACK, NULL, 
                    CFG_AUDIO_TASK_PRIORITY, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "Task create failed");
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

void audio_deinit(void) {
    if (s_task) {
        vTaskDelete(s_task);
        s_task = NULL;
    }
    if (s_queue) {
        vQueueDelete(s_queue);
        s_queue = NULL;
    }
    if (s_i2s_handle) {
        i2s_channel_disable(s_i2s_handle);
        i2s_del_channel(s_i2s_handle);
        s_i2s_handle = NULL;
    }
    if (s_mp3_decoder) {
        MP3FreeDecoder(s_mp3_decoder);
        s_mp3_decoder = NULL;
    }
    ESP_LOGI(TAG, "Deinitialized");
}

esp_err_t audio_play(sound_type_t sound) {
    if (!s_queue) return ESP_ERR_INVALID_STATE;
    if (sound >= SOUND_MAX) return ESP_ERR_INVALID_ARG;
    
    audio_cmd_t cmd = { .sound = sound };
    
    /* Stop current, play new */
    s_stop_flag = true;
    xQueueReset(s_queue);
    vTaskDelay(pdMS_TO_TICKS(5));
    
    if (xQueueSend(s_queue, &cmd, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t audio_tone(uint16_t freq_hz, uint16_t duration_ms) {
    if (!s_i2s_handle) return ESP_ERR_INVALID_STATE;
    play_tone(freq_hz, duration_ms);
    return ESP_OK;
}

void audio_stop(void) {
    s_stop_flag = true;
    if (s_queue) xQueueReset(s_queue);
}

void audio_set_volume(uint8_t volume) {
    s_volume = (volume > 100) ? 100 : volume;
    ESP_LOGI(TAG, "Volume: %d%%", s_volume);
}

uint8_t audio_get_volume(void) {
    return s_volume;
}
