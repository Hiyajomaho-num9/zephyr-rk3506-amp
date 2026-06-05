# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(.)
zephyr_include_directories(${ZEPHYR_BASE}/modules/cmsis)
zephyr_include_directories_ifdef(CONFIG_SOC_RK3506_RPMSG_LITE
  rpmsg_lite/include
  rpmsg_lite/include/environment/bm
  rpmsg_lite/include/platform/RK3506
)
zephyr_sources(soc.c)
zephyr_sources_ifdef(CONFIG_SOC_RK3506_RPMSG_LITE
  rk3506_rpmsg_platform.c
  rpmsg_lite/common/llist.c
  rpmsg_lite/rpmsg_lite/rpmsg_env_bm.c
  rpmsg_lite/rpmsg_lite/rpmsg_lite.c
  rpmsg_lite/rpmsg_lite/rpmsg_ns.c
  rpmsg_lite/virtio/virtqueue.c
)

set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/arm/cortex_a_r/scripts/linker.ld CACHE INTERNAL "")
