# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(firmware_v0.4_esp_idf)

# --- LVGL config ---
# LVGL needs lv_conf.h. We keep it at the project root. Two things make it
# reachable from every component that compiles LVGL headers:
#   1. Force LVGL's `#include "lv_conf.h"` branch (simple include, no path).
#   2. Add the project root to the global include search path so that
#      simple include actually resolves.
idf_build_set_property(COMPILE_OPTIONS "-DLV_CONF_INCLUDE_SIMPLE"  APPEND)
idf_build_set_property(COMPILE_OPTIONS "-I${CMAKE_CURRENT_LIST_DIR}" APPEND)