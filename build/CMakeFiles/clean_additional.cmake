# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "")
  file(REMOVE_RECURSE
  "AS608-ESP32s3.bin"
  "AS608-ESP32s3.map"
  "api.js.S"
  "app.js.S"
  "boot.mp3.S"
  "bootloader\\bootloader.bin"
  "bootloader\\bootloader.elf"
  "bootloader\\bootloader.map"
  "config\\sdkconfig.cmake"
  "config\\sdkconfig.h"
  "delete_ok.mp3.S"
  "enroll_ok.mp3.S"
  "enroll_start.mp3.S"
  "enroll_step.mp3.S"
  "esp-idf\\esptool_py\\flasher_args.json.in"
  "esp-idf\\mbedtls\\x509_crt_bundle"
  "flash_app_args"
  "flash_bootloader_args"
  "flash_project_args"
  "flasher_args.json"
  "index.html.S"
  "ldgen_libraries"
  "ldgen_libraries.in"
  "match_fail.mp3.S"
  "match_ok.mp3.S"
  "project_elf_src_esp32s3.c"
  "style.css.S"
  "wifi_configuration.html.S"
  "wifi_configuration_done.html.S"
  "ws.js.S"
  "x509_crt_bundle.S"
  )
endif()
