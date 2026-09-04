# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

get_filename_component(RK3506_ZEPHYR_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/../../../.."
  ABSOLUTE
)
# Same compatibility glue as rk3506_pingpong: SDK direct builds invoke this
# as a freestanding app.  NOTE: do NOT append RK3506_ZEPHYR_ROOT to
# EXTRA_ZEPHYR_MODULES here — the repo root exposes zephyr/{CMakeLists.txt,
# Kconfig}, so west's module scanner treats it as a module and re-sources
# the main Kconfig recursively.  BOARD_ROOT/SOC_ROOT/DTS_ROOT below already
# make the board/soc/dts visible.
list(APPEND BOARD_ROOT "${RK3506_ZEPHYR_ROOT}")
list(APPEND SOC_ROOT "${RK3506_ZEPHYR_ROOT}")
list(APPEND DTS_ROOT "${RK3506_ZEPHYR_ROOT}")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gpio_power_cycle)

target_include_directories(app PRIVATE
  src
)

target_sources(app PRIVATE
  src/main.c
)
