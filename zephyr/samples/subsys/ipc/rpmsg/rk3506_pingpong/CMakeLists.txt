# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

get_filename_component(RK3506_ZEPHYR_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.."
  ABSOLUTE
)
# SDK direct builds invoke this as a freestanding app. Keep these roots as
# compatibility glue. This is a complete Zephyr source tree, not an external
# module; registering it as one breaks when the SDK uses a symlinked base.
list(APPEND BOARD_ROOT "${RK3506_ZEPHYR_ROOT}")
list(APPEND SOC_ROOT "${RK3506_ZEPHYR_ROOT}")
list(APPEND DTS_ROOT "${RK3506_ZEPHYR_ROOT}")

# SDK builds pass optional overlays through EXTRA_DTC_OVERLAY_FILE. Direct
# CMake builds use the same standard Zephyr option.

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rk3506_rpmsg_pingpong)

target_include_directories(app PRIVATE
  src
)

target_sources(app PRIVATE
  src/main.c
  src/rk3506_rpmsg_demo.c
)
