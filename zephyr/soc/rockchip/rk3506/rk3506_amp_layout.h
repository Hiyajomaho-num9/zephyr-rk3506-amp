/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_ROCKCHIP_RK3506_AMP_LAYOUT_H_
#define ZEPHYR_SOC_ROCKCHIP_RK3506_AMP_LAYOUT_H_

#include <stdint.h>

/*
 * Keep this layout aligned with Linux:
 *   kernel-6.1/arch/arm/boot/dts/rk3506-amp.dtsi
 *   kernel-6.1/arch/arm/boot/dts/OK3506-S-MINI_amp_nand.dts
 *
 * and with the RT-Thread RK3506 AMP baseline:
 *   rtos/bsp/rockchip/rk3506-32/rtconfig.py
 *   rtos/bsp/rockchip/rk3506-32/gcc_arm.ld.S
 */

#define RK3506_AMP_SHMEM_BASE       0x03b00000U
#define RK3506_AMP_SHMEM_SIZE       0x00100000U

#define RK3506_RPMSG_SHMEM_BASE     0x03c00000U
#define RK3506_RPMSG_SHMEM_SIZE     0x00200000U

#define RK3506_RPMSG_VRING_BASE     RK3506_RPMSG_SHMEM_BASE
#define RK3506_RPMSG_VRING_SIZE     0x00020000U
#define RK3506_RPMSG_VRING0_BASE    RK3506_RPMSG_VRING_BASE
#define RK3506_RPMSG_VRING1_BASE    (RK3506_RPMSG_VRING_BASE + 0x00008000U)

#define RK3506_RPMSG_DMA_BASE       0x03d00000U
#define RK3506_RPMSG_DMA_SIZE       0x00100000U

#define RK3506_AMP_FIRMWARE_BASE    0x03e00000U
#define RK3506_AMP_FIRMWARE_SIZE    0x00100000U

#define RK3506_RPMSG_LINK_ID        0x02U
#define RK3506_RPMSG_EPT_ADDR       0x3003U
#define RK3506_RPMSG_CH_NAME        "rpmsg-ap3-ch0"
#define RK3506_RPMSG_MBOX_MAGIC     0x524d5347U

/*
 * Phase 5.2 policy tag for the shared-memory MMU mapping.
 * ASCII "STRO": strongly ordered, read/write, non-executable, L1-section
 * eligible. This mirrors RT-Thread's RK3506 UNCACHED_MEM usage for
 * RTT_SHMEM and LINUX_RPMSG.
 */
#define RK3506_AMP_SHARED_MMU_POLICY_STRO 0x5354524fU

#define RK3506_AMP_CACHE_POLICY_ICACHE_ONLY 0x49434f4eU
#define RK3506_AMP_CACHE_POLICY_IDCACHE 0x49444348U

/*
 * Phase 5.4 preflight tag. The runtime still keeps SCTLR.C disabled; these
 * fields only expose the MMU/cache boundary before we try D-cache enablement.
 */
#define RK3506_AMP_CACHE_PREFLIGHT_POLICY 0x50464c54U

/*
 * Phase 5.5 D-cache validation tag. Shared AMP/RPMsg memory remains strongly
 * ordered; only normal private Zephyr mappings become D-cacheable at runtime.
 */
#define RK3506_AMP_CACHE_DCACHE_POLICY 0x44434143U

/*
 * Phase 5.6/5.7 combined stress tag. ASCII "THRS": a private Zephyr
 * D-cache thrash thread is running while Linux stresses RPMsg.
 */
#define RK3506_AMP_CACHE_THRASH_POLICY 0x54485253U

#endif /* ZEPHYR_SOC_ROCKCHIP_RK3506_AMP_LAYOUT_H_ */
