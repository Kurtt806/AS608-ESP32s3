/**
 * @file config.h
 * @brief Hardware Configuration - GPIO and System Settings
 */

#ifndef COMMON_CONFIG_H
#define COMMON_CONFIG_H

#include "driver/uart.h"
#include "driver/gpio.h"

/* ============================================================================
 * AS608 Fingerprint Sensor
 * ============================================================================ */
#define CFG_AS608_UART_PORT         UART_NUM_1
#define CFG_AS608_TX_GPIO           GPIO_NUM_12
#define CFG_AS608_RX_GPIO           GPIO_NUM_13
#define CFG_AS608_RST_GPIO          GPIO_NUM_NC
#define CFG_AS608_PWR_EN_GPIO       GPIO_NUM_NC
#define CFG_AS608_BAUD_RATE         57600
#define CFG_AS608_ADDRESS           0xFFFFFFFF
#define CFG_AS608_PASSWORD          0x00000000
#define CFG_AS608_TIMEOUT_MS        1500
#define CFG_AS608_LIBRARY_SIZE      162

/* ============================================================================
 * Finger Module Timing
 * ============================================================================ */
#define CFG_FINGER_TASK_STACK       3072
#define CFG_FINGER_TASK_PRIORITY    5
#define CFG_FINGER_QUEUE_SIZE       8
#define CFG_FINGER_SCAN_INTERVAL_MS 1000
#define CFG_FINGER_SCAN_FAST_MS     300
#define CFG_FINGER_IDLE_THRESHOLD   3

/* ============================================================================
 * Button Configuration
 * ============================================================================ */
#define CFG_BTN_BOOT_GPIO           GPIO_NUM_8
#define CFG_BTN_LONG_PRESS_MS       5000
#define CFG_BTN_SHORT_PRESS_MS      180

/* ============================================================================
 * MAX98357 Audio
 * ============================================================================ */
#define CFG_I2S_WS                  GPIO_NUM_7
#define CFG_I2S_BCK                 GPIO_NUM_6
#define CFG_I2S_DATA                GPIO_NUM_5
#define CFG_AUDIO_TASK_STACK        12288
#define CFG_AUDIO_TASK_PRIORITY     3
#define CFG_AUDIO_QUEUE_SIZE        8
#define CFG_AUDIO_DEFAULT_VOLUME    10
#define CFG_AUDIO_SAMPLE_RATE       44100

/* ============================================================================
 * WiFi Configuration
 * ============================================================================ */
#define CFG_WIFI_AP_PREFIX          "AS608-"
#define CFG_WIFI_LANGUAGE           "vi"
#define CFG_WIFI_CONNECT_TIMEOUT_MS 10000

/* ============================================================================
 * Web Server Configuration
 * ============================================================================ */
#define WEBSERVER_TASK_STACK_SIZE   8192
#define WEBSERVER_MAX_CLIENTS       4

#endif /* COMMON_CONFIG_H */
