# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources(regset_target_lib.c)
zephyr_library_sources(tmp103_target.c)
zephyr_library_property(ALLOW_EMPTY TRUE)
