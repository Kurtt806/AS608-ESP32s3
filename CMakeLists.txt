# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

# Firmware version - change this when releasing new version
set(PROJECT_VER "dev1.0.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(AS608-ESP32s3)