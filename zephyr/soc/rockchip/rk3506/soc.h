/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_ROCKCHIP_RK3506_SOC_H_
#define ZEPHYR_SOC_ROCKCHIP_RK3506_SOC_H_

#ifndef _ASMLANGUAGE

/*
 * Required by Zephyr's CMSIS wrapper for AArch32 Cortex-A/R cores.
 */
#define __CORTEX_A 7U

#include <errno.h>
#include <stdint.h>

uint32_t rk3506_soc_amp_shmem_mmu_attrs(void);
uint32_t rk3506_soc_rpmsg_mmu_attrs(void);
uint32_t rk3506_soc_cache_policy(void);
uint32_t rk3506_soc_sctlr_snapshot(void);
uint32_t rk3506_soc_cache_preflight_policy(void);
uint32_t rk3506_soc_ttbr0_snapshot(void);
uint32_t rk3506_soc_dacr_snapshot(void);
uint32_t rk3506_soc_ttbcr_snapshot(void);
uint32_t rk3506_soc_normal_code_mmu_attrs(void);
uint32_t rk3506_soc_normal_data_mmu_attrs(void);
uint32_t rk3506_soc_normal_rodata_mmu_attrs(void);

#endif /* !_ASMLANGUAGE */

#endif /* ZEPHYR_SOC_ROCKCHIP_RK3506_SOC_H_ */
