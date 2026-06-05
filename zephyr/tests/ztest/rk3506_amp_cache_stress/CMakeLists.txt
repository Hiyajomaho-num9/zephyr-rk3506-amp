# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

get_filename_component(RK3506_ZEPHYR_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/../../.."
  ABSOLUTE
)
# SDK direct builds invoke this as a freestanding app. Keep these roots as
# compatibility glue; zephyr/module.yml is the official module entry point.
list(APPEND BOARD_ROOT "${RK3506_ZEPHYR_ROOT}")
list(APPEND SOC_ROOT "${RK3506_ZEPHYR_ROOT}")
list(APPEND DTS_ROOT "${RK3506_ZEPHYR_ROOT}")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rk3506_amp_cache_stress)

target_include_directories(app PRIVATE
  src
)

target_sources(app PRIVATE
  src/main.c
  src/rk3506_rpmsg_demo.c
)

target_sources_ifdef(CONFIG_SOC_RK3506_AMP_PROBE app PRIVATE
  src/rk3506_amp_probe.c
)

target_sources_ifdef(CONFIG_SOC_RK3506_CACHE_THRASH app PRIVATE
  src/rk3506_cache_thrash.c
)
