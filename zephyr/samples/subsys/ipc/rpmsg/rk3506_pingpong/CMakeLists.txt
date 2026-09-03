# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

get_filename_component(RK3506_ZEPHYR_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.."
  ABSOLUTE
)
# SDK direct builds invoke this as a freestanding app. Keep these roots as
# compatibility glue; zephyr/module.yml is the official module entry point.
list(APPEND BOARD_ROOT "${RK3506_ZEPHYR_ROOT}")
list(APPEND SOC_ROOT "${RK3506_ZEPHYR_ROOT}")
list(APPEND DTS_ROOT "${RK3506_ZEPHYR_ROOT}")
list(APPEND EXTRA_ZEPHYR_MODULES "${RK3506_ZEPHYR_ROOT}")

# Optional debug overlay injection (set before cmake configure):
#   RK3506_DEBUG_DTS_OVERLAY=<path>  -> appended to DTC_OVERLAY_FILE
#   e.g. RK3506_DEBUG_DTS_OVERLAY=$PWD/boards/debug/enable_gpio2.overlay
# Must run before find_package(Zephyr).
if(DEFINED ENV{RK3506_DEBUG_DTS_OVERLAY})
  set(DTC_OVERLAY_FILE "$ENV{RK3506_DEBUG_DTS_OVERLAY}")
  message(STATUS "rk3506_pingpong: debug overlay: ${DTC_OVERLAY_FILE}")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rk3506_rpmsg_pingpong)

target_include_directories(app PRIVATE
  src
)

target_sources(app PRIVATE
  src/main.c
  src/rk3506_rpmsg_demo.c
)
