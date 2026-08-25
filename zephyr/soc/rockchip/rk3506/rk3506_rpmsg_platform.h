/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_ROCKCHIP_RK3506_RPMSG_PLATFORM_H_
#define ZEPHYR_SOC_ROCKCHIP_RK3506_RPMSG_PLATFORM_H_

#include <stdint.h>

void rk3506_rpmsg_platform_poll(void);
void rk3506_rpmsg_platform_force_irq_enable(void);
uint32_t rk3506_rpmsg_platform_rx_count(void);
uint32_t rk3506_rpmsg_platform_bad_count(void);
uint32_t rk3506_rpmsg_platform_tx_count(void);
uint32_t rk3506_rpmsg_platform_irq_count(void);
uint32_t rk3506_rpmsg_platform_irq_enable_count(void);
uint32_t rk3506_rpmsg_platform_irq_disable_count(void);
uint32_t rk3506_rpmsg_platform_irq_last_enable_vector(void);
uint32_t rk3506_rpmsg_platform_irq_last_disable_vector(void);
uint32_t rk3506_rpmsg_platform_compiled_flags(void);
uint32_t rk3506_rpmsg_platform_debug_status(void);
uint32_t rk3506_rpmsg_platform_last_status(void);
uint32_t rk3506_rpmsg_platform_last_cmd(void);
uint32_t rk3506_rpmsg_platform_last_data(void);
uint32_t rk3506_rpmsg_platform_poll_count(void);
uint32_t rk3506_rpmsg_platform_poll_hit_count(void);
uint32_t rk3506_rpmsg_platform_irq_configured(void);
uint32_t rk3506_rpmsg_platform_irq_number(void);
uint32_t rk3506_rpmsg_platform_gic_target_before(void);
uint32_t rk3506_rpmsg_platform_gic_target_after(void);
uint32_t rk3506_rpmsg_platform_gic_enable(void);
uint32_t rk3506_rpmsg_platform_gic_pending(void);
uint32_t rk3506_rpmsg_platform_gic_active(void);
uint32_t rk3506_rpmsg_platform_gic_priority(void);
uint32_t rk3506_rpmsg_platform_gic_dist_ctlr(void);
uint32_t rk3506_rpmsg_platform_gic_cpu_ctlr(void);
uint32_t rk3506_rpmsg_platform_gic_cpu_pmr(void);
uint32_t rk3506_rpmsg_platform_gic_group(void);
uint32_t rk3506_rpmsg_platform_gic_group_word(void);
uint32_t rk3506_rpmsg_platform_gic_icfgr(void);
uint32_t rk3506_rpmsg_platform_gic_icfgr_word(void);
uint32_t rk3506_rpmsg_platform_gic_enable_word(void);
uint32_t rk3506_rpmsg_platform_gic_pending_word(void);
uint32_t rk3506_rpmsg_platform_gic_active_word(void);
uint32_t rk3506_rpmsg_platform_gic_group_before(void);
uint32_t rk3506_rpmsg_platform_gic_group_value(void);
uint32_t rk3506_rpmsg_platform_gic_group_after(void);
uint32_t rk3506_rpmsg_platform_gic_enable_before(void);
uint32_t rk3506_rpmsg_platform_gic_enable_value(void);
uint32_t rk3506_rpmsg_platform_gic_enable_after(void);
uint32_t rk3506_rpmsg_platform_mbox2_a2b_inten(void);
uint32_t rk3506_rpmsg_platform_mbox2_a2b_status(void);
uint32_t rk3506_rpmsg_platform_mbox2_a2b_cmd(void);
uint32_t rk3506_rpmsg_platform_mbox2_a2b_data(void);
uint32_t rk3506_rpmsg_platform_mbox2_b2a_inten(void);
uint32_t rk3506_rpmsg_platform_mbox2_b2a_status(void);
uint32_t rk3506_rpmsg_platform_mbox2_b2a_cmd(void);
uint32_t rk3506_rpmsg_platform_mbox2_b2a_data(void);
uint32_t rk3506_rpmsg_platform_status_seen_mask(void);
uint32_t rk3506_rpmsg_platform_status_seen_count(void);

#endif /* ZEPHYR_SOC_ROCKCHIP_RK3506_RPMSG_PLATFORM_H_ */
