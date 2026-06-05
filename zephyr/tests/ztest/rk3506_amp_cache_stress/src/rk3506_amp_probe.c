/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/barrier.h>

#include "rk3506_amp_probe.h"
#include "rk3506_amp_layout.h"
#include "rk3506_cache_thrash.h"
#include "rk3506_rpmsg_demo.h"
#include "rk3506_rpmsg_platform.h"
#include "soc.h"

#define RK3506_AMP_SHMEM_PROBE_MAGIC 0x524b5a50U
#define RK3506_AMP_SHMEM_PROBE_VERSION 14U

struct rk3506_amp_shmem_probe {
	uint32_t magic;
	uint32_t version;
	uint32_t counter;
	uint32_t uptime_ms_lo;
	uint32_t uptime_ms_hi;
	uint32_t last_status;
	uint32_t inverse_counter;
	uint32_t mbox_debug_status;
	uint32_t mbox_irq_count;
	uint32_t mbox_last_status;
	uint32_t mbox_last_cmd;
	uint32_t mbox_last_data;
	uint32_t mbox_bad_count;
	uint32_t mbox_poll_count;
	uint32_t mbox_poll_hit_count;
	uint32_t mbox_irq_configured;
	uint32_t mbox_irq_number;
	uint32_t gic_target_before;
	uint32_t gic_target_after;
	uint32_t gic_enable;
	uint32_t gic_pending;
	uint32_t gic_active;
	uint32_t gic_priority;
	uint32_t gic_dist_ctlr;
	uint32_t gic_cpu_ctlr;
	uint32_t gic_cpu_pmr;
	uint32_t mbox_status_seen_mask;
	uint32_t mbox_status_seen_count;
	uint32_t gic_group;
	uint32_t gic_group_word;
	uint32_t gic_icfgr;
	uint32_t gic_icfgr_word;
	uint32_t gic_enable_word;
	uint32_t gic_pending_word;
	uint32_t gic_active_word;
	uint32_t mbox2_a2b_inten;
	uint32_t mbox2_a2b_status;
	uint32_t mbox2_b2a_inten;
	uint32_t mbox2_b2a_status;
	uint32_t mbox2_a2b_cmd;
	uint32_t mbox2_a2b_data;
	uint32_t mbox2_b2a_cmd;
	uint32_t mbox2_b2a_data;
	uint32_t mbox_irq_enable_count;
	uint32_t mbox_irq_disable_count;
	uint32_t mbox_irq_last_enable_vector;
	uint32_t mbox_irq_last_disable_vector;
	uint32_t compiled_flags;
	uint32_t gic_group_before;
	uint32_t gic_group_value;
	uint32_t gic_group_after;
	uint32_t gic_enable_before;
	uint32_t gic_enable_value;
	uint32_t gic_enable_after;
	uint32_t mmu_policy;
	uint32_t amp_shmem_mmu_attrs;
	uint32_t rpmsg_mmu_attrs;
	uint32_t sctlr_snapshot;
	uint32_t cache_policy;
	uint32_t sctlr_i_bit;
	uint32_t sctlr_c_bit;
	uint32_t sctlr_a_bit;
	uint32_t preflight_policy;
	uint32_t sctlr_m_bit;
	uint32_t ttbr0_snapshot;
	uint32_t dacr_snapshot;
	uint32_t ttbcr_snapshot;
	uint32_t ttbr0_low_bits;
	uint32_t normal_code_mmu_attrs;
	uint32_t normal_data_mmu_attrs;
	uint32_t normal_rodata_mmu_attrs;
	uint32_t thrash_policy;
	uint32_t thrash_counter;
	uint32_t thrash_signature;
	uint32_t thrash_error_count;
};

static volatile struct rk3506_amp_shmem_probe *const shmem_probe =
	(volatile struct rk3506_amp_shmem_probe *)RK3506_AMP_SHMEM_BASE;

void rk3506_amp_probe_update(uint32_t counter)
{
	int64_t uptime = k_uptime_get();

	shmem_probe->magic = RK3506_AMP_SHMEM_PROBE_MAGIC;
	shmem_probe->version = RK3506_AMP_SHMEM_PROBE_VERSION;
	shmem_probe->counter = counter;
	shmem_probe->uptime_ms_lo = (uint32_t)uptime;
	shmem_probe->uptime_ms_hi = (uint32_t)((uint64_t)uptime >> 32);
	shmem_probe->last_status =
		((rk3506_rpmsg_demo_rx_count() & 0xffU) << 24) |
		((rk3506_rpmsg_demo_tx_count() & 0xffU) << 16) |
		((rk3506_rpmsg_platform_rx_count() & 0xffU) << 8) |
		(rk3506_rpmsg_demo_ns_sent() & 0xffU);
	shmem_probe->inverse_counter = ~counter;
	shmem_probe->mbox_debug_status = rk3506_rpmsg_platform_debug_status();
	shmem_probe->mbox_irq_count = rk3506_rpmsg_platform_irq_count();
	shmem_probe->mbox_last_status = rk3506_rpmsg_platform_last_status();
	shmem_probe->mbox_last_cmd = rk3506_rpmsg_platform_last_cmd();
	shmem_probe->mbox_last_data = rk3506_rpmsg_platform_last_data();
	shmem_probe->mbox_bad_count = rk3506_rpmsg_platform_bad_count();
	shmem_probe->mbox_poll_count = rk3506_rpmsg_platform_poll_count();
	shmem_probe->mbox_poll_hit_count = rk3506_rpmsg_platform_poll_hit_count();
	shmem_probe->mbox_irq_configured = rk3506_rpmsg_platform_irq_configured();
	shmem_probe->mbox_irq_number = rk3506_rpmsg_platform_irq_number();
	shmem_probe->gic_target_before = rk3506_rpmsg_platform_gic_target_before();
	shmem_probe->gic_target_after = rk3506_rpmsg_platform_gic_target_after();
	shmem_probe->gic_enable = rk3506_rpmsg_platform_gic_enable();
	shmem_probe->gic_pending = rk3506_rpmsg_platform_gic_pending();
	shmem_probe->gic_active = rk3506_rpmsg_platform_gic_active();
	shmem_probe->gic_priority = rk3506_rpmsg_platform_gic_priority();
	shmem_probe->gic_dist_ctlr = rk3506_rpmsg_platform_gic_dist_ctlr();
	shmem_probe->gic_cpu_ctlr = rk3506_rpmsg_platform_gic_cpu_ctlr();
	shmem_probe->gic_cpu_pmr = rk3506_rpmsg_platform_gic_cpu_pmr();
	shmem_probe->mbox_status_seen_mask = rk3506_rpmsg_platform_status_seen_mask();
	shmem_probe->mbox_status_seen_count = rk3506_rpmsg_platform_status_seen_count();
	shmem_probe->gic_group = rk3506_rpmsg_platform_gic_group();
	shmem_probe->gic_group_word = rk3506_rpmsg_platform_gic_group_word();
	shmem_probe->gic_icfgr = rk3506_rpmsg_platform_gic_icfgr();
	shmem_probe->gic_icfgr_word = rk3506_rpmsg_platform_gic_icfgr_word();
	shmem_probe->gic_enable_word = rk3506_rpmsg_platform_gic_enable_word();
	shmem_probe->gic_pending_word = rk3506_rpmsg_platform_gic_pending_word();
	shmem_probe->gic_active_word = rk3506_rpmsg_platform_gic_active_word();
	shmem_probe->mbox2_a2b_inten = rk3506_rpmsg_platform_mbox2_a2b_inten();
	shmem_probe->mbox2_a2b_status = rk3506_rpmsg_platform_mbox2_a2b_status();
	shmem_probe->mbox2_b2a_inten = rk3506_rpmsg_platform_mbox2_b2a_inten();
	shmem_probe->mbox2_b2a_status = rk3506_rpmsg_platform_mbox2_b2a_status();
	shmem_probe->mbox2_a2b_cmd = rk3506_rpmsg_platform_mbox2_a2b_cmd();
	shmem_probe->mbox2_a2b_data = rk3506_rpmsg_platform_mbox2_a2b_data();
	shmem_probe->mbox2_b2a_cmd = rk3506_rpmsg_platform_mbox2_b2a_cmd();
	shmem_probe->mbox2_b2a_data = rk3506_rpmsg_platform_mbox2_b2a_data();
	shmem_probe->mbox_irq_enable_count = rk3506_rpmsg_platform_irq_enable_count();
	shmem_probe->mbox_irq_disable_count = rk3506_rpmsg_platform_irq_disable_count();
	shmem_probe->mbox_irq_last_enable_vector =
		rk3506_rpmsg_platform_irq_last_enable_vector();
	shmem_probe->mbox_irq_last_disable_vector =
		rk3506_rpmsg_platform_irq_last_disable_vector();
	shmem_probe->compiled_flags = rk3506_rpmsg_platform_compiled_flags();
	shmem_probe->gic_group_before = rk3506_rpmsg_platform_gic_group_before();
	shmem_probe->gic_group_value = rk3506_rpmsg_platform_gic_group_value();
	shmem_probe->gic_group_after = rk3506_rpmsg_platform_gic_group_after();
	shmem_probe->gic_enable_before = rk3506_rpmsg_platform_gic_enable_before();
	shmem_probe->gic_enable_value = rk3506_rpmsg_platform_gic_enable_value();
	shmem_probe->gic_enable_after = rk3506_rpmsg_platform_gic_enable_after();
	shmem_probe->mmu_policy = RK3506_AMP_SHARED_MMU_POLICY_STRO;
	shmem_probe->amp_shmem_mmu_attrs = rk3506_soc_amp_shmem_mmu_attrs();
	shmem_probe->rpmsg_mmu_attrs = rk3506_soc_rpmsg_mmu_attrs();
	shmem_probe->sctlr_snapshot = rk3506_soc_sctlr_snapshot();
	shmem_probe->cache_policy = rk3506_soc_cache_policy();
	shmem_probe->sctlr_i_bit = (shmem_probe->sctlr_snapshot >> 12) & 0x1U;
	shmem_probe->sctlr_c_bit = (shmem_probe->sctlr_snapshot >> 2) & 0x1U;
	shmem_probe->sctlr_a_bit = (shmem_probe->sctlr_snapshot >> 1) & 0x1U;
	shmem_probe->preflight_policy = rk3506_soc_cache_preflight_policy();
	shmem_probe->sctlr_m_bit = shmem_probe->sctlr_snapshot & 0x1U;
	shmem_probe->ttbr0_snapshot = rk3506_soc_ttbr0_snapshot();
	shmem_probe->dacr_snapshot = rk3506_soc_dacr_snapshot();
	shmem_probe->ttbcr_snapshot = rk3506_soc_ttbcr_snapshot();
	shmem_probe->ttbr0_low_bits = shmem_probe->ttbr0_snapshot & 0x7fU;
	shmem_probe->normal_code_mmu_attrs = rk3506_soc_normal_code_mmu_attrs();
	shmem_probe->normal_data_mmu_attrs = rk3506_soc_normal_data_mmu_attrs();
	shmem_probe->normal_rodata_mmu_attrs = rk3506_soc_normal_rodata_mmu_attrs();
	shmem_probe->thrash_policy = RK3506_AMP_CACHE_THRASH_POLICY;
	shmem_probe->thrash_counter = rk3506_cache_thrash_counter();
	shmem_probe->thrash_signature = rk3506_cache_thrash_signature();
	shmem_probe->thrash_error_count = rk3506_cache_thrash_error_count();
	barrier_dmem_fence_full();
}
