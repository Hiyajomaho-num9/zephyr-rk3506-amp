/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/sys/util.h>

#ifdef CONFIG_GIC_V2
#include <zephyr/drivers/interrupt_controller/gic.h>
#endif

#include "rpmsg_env.h"
#include "rpmsg_platform.h"
#include "rk3506_amp_layout.h"
#include "rk3506_rpmsg_platform.h"

#define RK3506_MBOX0_BASE 0xff290000U
#define RK3506_MBOX2_BASE 0xff292000U

#define RK3506_MBOX_A2B_INTEN 0x00U
#define RK3506_MBOX_A2B_STATUS 0x04U
#define RK3506_MBOX_A2B_CMD 0x08U
#define RK3506_MBOX_A2B_DATA 0x0cU
#define RK3506_MBOX_B2A_INTEN 0x10U
#define RK3506_MBOX_B2A_STATUS 0x14U
#define RK3506_MBOX_B2A_CMD 0x18U
#define RK3506_MBOX_B2A_DATA 0x1cU

#define RK3506_MBOX_STATUS_TX_DONE BIT(0)
#define RK3506_MBOX_STATUS_RX_DONE BIT(1)
#define RK3506_MBOX_WRITE_MASK_SHIFT 16U
#define RK3506_MBOX_TRIGGER_SHIFT 8U
#define RK3506_MBOX_INTEN_CH0 (BIT(0) | BIT(RK3506_MBOX_TRIGGER_SHIFT))
#define RK3506_MBOX_INTEN_WRITE_MASK(bits) ((bits) << RK3506_MBOX_WRITE_MASK_SHIFT)
#define RK3506_RPMSG_MBOX_BB2_IRQ CONFIG_SOC_RK3506_RPMSG_MBOX_BB2_IRQ
#define RK3506_RPMSG_MBOX_IRQ_PRIORITY 0xd0U
#define RK3506_GICV2_BAD_VALUE 0xffffffffU

static uint32_t mailbox_rx_count;
static uint32_t mailbox_tx_count;
static uint32_t mailbox_irq_count;
static uint32_t mailbox_irq_enable_count;
static uint32_t mailbox_irq_disable_count;
static uint32_t mailbox_irq_last_enable_vector;
static uint32_t mailbox_irq_last_disable_vector;
static uint32_t mailbox_bad_count;
static uint32_t mailbox_last_status;
static uint32_t mailbox_last_cmd;
static uint32_t mailbox_last_data;
static uint32_t mailbox_poll_count;
static uint32_t mailbox_poll_hit_count;
static uint32_t mailbox_irq_configured;
static uint32_t mailbox_status_seen_mask;
static uint32_t mailbox_status_seen_count;
static uint32_t mailbox_gic_target_before = RK3506_GICV2_BAD_VALUE;
static uint32_t mailbox_gic_target_after = RK3506_GICV2_BAD_VALUE;
static uint32_t mailbox_gic_group_before = RK3506_GICV2_BAD_VALUE;
static uint32_t mailbox_gic_group_value = RK3506_GICV2_BAD_VALUE;
static uint32_t mailbox_gic_group_after = RK3506_GICV2_BAD_VALUE;
static uint32_t mailbox_gic_enable_before = RK3506_GICV2_BAD_VALUE;
static uint32_t mailbox_gic_enable_value = RK3506_GICV2_BAD_VALUE;
static uint32_t mailbox_gic_enable_after = RK3506_GICV2_BAD_VALUE;
static uint32_t first_notify;
static void *platform_lock;

static void rk3506_rpmsg_platform_enable_physical_irq(uint32_t irq);

static uint32_t mmio_read32(uint32_t addr)
{
	return *(volatile uint32_t *)addr;
}

static void mmio_write32(uint32_t val, uint32_t addr)
{
	*(volatile uint32_t *)addr = val;
	barrier_dmem_fence_full();
}

static void mbox_clear_rx(uint32_t base)
{
	mmio_write32(RK3506_MBOX_STATUS_TX_DONE, base + RK3506_MBOX_A2B_STATUS);
}

static void mbox_enable_a2b_rx(uint32_t base)
{
	/*
	 * Match Rockchip HAL_MBOX_Init(..., RL_MBOX_B2A) for remote CPU.
	 * Bit0 enables ch0 interrupt; bit8 selects data-trigger mode. The
	 * upper halfword is the write-mask used by RK3506 mailbox v2.
	 */
	mmio_write32(RK3506_MBOX_INTEN_WRITE_MASK(RK3506_MBOX_INTEN_CH0) |
		     RK3506_MBOX_INTEN_CH0,
		     base + RK3506_MBOX_A2B_INTEN);
	mbox_clear_rx(base);
}

static void mbox_setup_b2a_tx(uint32_t base)
{
	/* Linux uses data-trigger mode: write CMD first, then DATA triggers. */
	mmio_write32(RK3506_MBOX_INTEN_WRITE_MASK(RK3506_MBOX_INTEN_CH0) |
		     RK3506_MBOX_INTEN_CH0,
		     base + RK3506_MBOX_B2A_INTEN);
	mmio_write32(RK3506_MBOX_STATUS_TX_DONE, base + RK3506_MBOX_B2A_STATUS);
}

static int mbox_send_b2a(uint32_t base, uint32_t link_id)
{
	uint32_t retry;

	for (retry = 0U; retry < 1000U; retry++) {
		if ((mmio_read32(base + RK3506_MBOX_B2A_STATUS) &
		     RK3506_MBOX_STATUS_TX_DONE) == 0U) {
			mmio_write32(link_id & 0xffU, base + RK3506_MBOX_B2A_CMD);
			mmio_write32(RK3506_RPMSG_MBOX_MAGIC, base + RK3506_MBOX_B2A_DATA);
			mailbox_tx_count++;
			return 0;
		}
		k_busy_wait(10);
	}

	return -1;
}

static bool rk3506_rpmsg_platform_handle_rx(bool allow_statusless_kick)
{
	uint32_t cmd;
	uint32_t data;
	uint32_t link_id;
	bool has_status;
	bool initial_statusless_kick;

	mailbox_last_status = mmio_read32(RK3506_MBOX2_BASE +
					  RK3506_MBOX_A2B_STATUS);
	mailbox_last_cmd = mmio_read32(RK3506_MBOX2_BASE + RK3506_MBOX_A2B_CMD);
	mailbox_last_data = mmio_read32(RK3506_MBOX2_BASE + RK3506_MBOX_A2B_DATA);

	has_status = (mailbox_last_status & RK3506_MBOX_STATUS_TX_DONE) != 0U;
	if (has_status) {
		mailbox_status_seen_mask |= mailbox_last_status;
		mailbox_status_seen_count++;
	}

	initial_statusless_kick = allow_statusless_kick && !has_status && (first_notify == 0U) &&
				  ((mailbox_last_cmd & 0xffU) == RK3506_RPMSG_LINK_ID) &&
				  (mailbox_last_data == RK3506_RPMSG_MBOX_MAGIC);

	if (!has_status && !initial_statusless_kick) {
		return false;
	}

	cmd = mailbox_last_cmd;
	data = mailbox_last_data;
	if (has_status) {
		mbox_clear_rx(RK3506_MBOX2_BASE);
	}

	mailbox_rx_count++;
	if (data != RK3506_RPMSG_MBOX_MAGIC) {
		mailbox_bad_count++;
	}

	link_id = cmd & 0xffU;
	if (link_id == 0U) {
		link_id = RK3506_RPMSG_LINK_ID;
	}

	if (first_notify == 0U) {
		env_isr(RL_GET_VQ_ID(link_id, 0));
		first_notify = 1U;
	} else {
		env_isr(RL_GET_VQ_ID(link_id, 1));
	}

	return true;
}

void rk3506_rpmsg_platform_poll(void)
{
	mailbox_poll_count++;
	if (rk3506_rpmsg_platform_handle_rx(true)) {
		mailbox_poll_hit_count++;
	}
}

void rk3506_rpmsg_platform_force_irq_enable(void)
{
#ifdef CONFIG_SOC_RK3506_RPMSG_MBOX_IRQ
	rk3506_rpmsg_platform_enable_physical_irq(RK3506_RPMSG_MBOX_BB2_IRQ);
#endif
}

#ifdef CONFIG_SOC_RK3506_RPMSG_MBOX_IRQ
static void rk3506_rpmsg_mbox_isr(const void *arg)
{
	ARG_UNUSED(arg);

	mailbox_irq_count++;
	(void)rk3506_rpmsg_platform_handle_rx(false);
}
#endif

#ifdef CONFIG_GIC_V2
static uint32_t gic_irq_bit(uint32_t irq)
{
	return BIT(irq & 31U);
}

static uint32_t gic_irq_word_addr(uint32_t base, uint32_t irq)
{
	return base + ((irq / 32U) * 4U);
}

static uint32_t gic_irq_word(uint32_t reg_base, uint32_t irq)
{
	return sys_read32(gic_irq_word_addr(reg_base, irq));
}

static uint32_t gic_irq_group_word_addr(uint32_t irq)
{
	return GICD_IGROUPRn + ((irq / 32U) * 4U);
}

static uint32_t gic_irq_icfgr_word_addr(uint32_t irq)
{
	return GICD_ICFGRn + ((irq / 16U) * 4U);
}
#endif

static void rk3506_rpmsg_platform_route_irq(uint32_t irq)
{
#ifdef CONFIG_GIC_V2
	mailbox_gic_target_before = sys_read8(GICD_ITARGETSRn + irq);
#if defined(CONFIG_SOC_RK3506_RPMSG_MBOX_GIC_ROUTE)
	/*
	 * Legacy path (GIC_SAFE_CONFIG disabled): force the target byte.
	 * With GIC_SAFE_CONFIG, arm_gic_irq_enable() ORs this core's mask
	 * into GICD_ITARGETSR when the IRQ is enabled below and never clears
	 * bits that belong to Linux, so nothing has to be written here.
	 */
	sys_write8((uint8_t)CONFIG_SOC_RK3506_RPMSG_MBOX_GIC_TARGET_MASK,
		   GICD_ITARGETSRn + irq);
#endif
	mailbox_gic_target_after = sys_read8(GICD_ITARGETSRn + irq);
#else
	ARG_UNUSED(irq);
#endif
}

static void rk3506_rpmsg_platform_group_irq(uint32_t irq)
{
#ifdef CONFIG_GIC_V2
	uint32_t addr = gic_irq_group_word_addr(irq);
	uint32_t val = sys_read32(addr);

	mailbox_gic_group_before = val;
#if defined(CONFIG_SOC_RK3506_RPMSG_MBOX_GIC_GROUP1)
	val |= gic_irq_bit(irq);
#else
	val &= ~gic_irq_bit(irq);
#endif
	mailbox_gic_group_value = val;
	sys_write32(val, addr);
	barrier_dmem_fence_full();
	mailbox_gic_group_after = sys_read32(addr);
#else
	ARG_UNUSED(irq);
#endif
}

static void rk3506_rpmsg_platform_enable_physical_irq(uint32_t irq)
{
#ifdef CONFIG_GIC_V2
	uint32_t addr = gic_irq_word_addr(GICD_ISENABLERn, irq);
	uint32_t bit = gic_irq_bit(irq);

	mailbox_gic_enable_before = sys_read32(addr);
	mailbox_gic_enable_value = bit;
	irq_enable(irq);
	sys_write32(bit, addr);
	barrier_dmem_fence_full();
	mailbox_gic_enable_after = sys_read32(addr);
#else
	irq_enable(irq);
#endif
}

static void rk3506_rpmsg_platform_clear_irq_state(uint32_t irq)
{
#ifdef CONFIG_GIC_V2
	uint32_t bit = gic_irq_bit(irq);

	sys_write32(bit, gic_irq_word_addr(GICD_ICPENDRn, irq));
	sys_write32(bit, gic_irq_word_addr(GICD_ICACTIVERn, irq));
#else
	ARG_UNUSED(irq);
#endif
}

static uint32_t rk3506_rpmsg_platform_gic_read_bit(uint32_t reg_base)
{
#ifdef CONFIG_GIC_V2
	uint32_t irq = RK3506_RPMSG_MBOX_BB2_IRQ;

	return (sys_read32(gic_irq_word_addr(reg_base, irq)) &
		gic_irq_bit(irq)) != 0U;
#else
	ARG_UNUSED(reg_base);
	return RK3506_GICV2_BAD_VALUE;
#endif
}

uint32_t rk3506_rpmsg_platform_rx_count(void)
{
	return mailbox_rx_count;
}

uint32_t rk3506_rpmsg_platform_bad_count(void)
{
	return mailbox_bad_count;
}

uint32_t rk3506_rpmsg_platform_tx_count(void)
{
	return mailbox_tx_count;
}

uint32_t rk3506_rpmsg_platform_irq_count(void)
{
	return mailbox_irq_count;
}

uint32_t rk3506_rpmsg_platform_irq_enable_count(void)
{
	return mailbox_irq_enable_count;
}

uint32_t rk3506_rpmsg_platform_irq_disable_count(void)
{
	return mailbox_irq_disable_count;
}

uint32_t rk3506_rpmsg_platform_irq_last_enable_vector(void)
{
	return mailbox_irq_last_enable_vector;
}

uint32_t rk3506_rpmsg_platform_irq_last_disable_vector(void)
{
	return mailbox_irq_last_disable_vector;
}

uint32_t rk3506_rpmsg_platform_compiled_flags(void)
{
	uint32_t flags = 0U;

#ifdef CONFIG_SOC_RK3506_RPMSG_MBOX_IRQ
	flags |= BIT(0);
#endif
#ifdef CONFIG_SOC_RK3506_RPMSG_MBOX_GIC_ROUTE
	flags |= BIT(1);
#endif
#ifdef CONFIG_SOC_RK3506_RPMSG_MBOX_GIC_GROUP1
	flags |= BIT(2);
#endif
#ifdef CONFIG_GIC_V2
	flags |= BIT(3);
#endif
#ifdef CONFIG_SOC_RK3506_RPMSG_IRQ_ONLY_AFTER_NS
	flags |= BIT(4);
#endif
#ifdef CONFIG_SOC_RK3506_RPMSG_KEEP_IRQ_ENABLE
	flags |= BIT(5);
#endif
#ifdef CONFIG_GIC_SAFE_CONFIG
	flags |= BIT(6);
#endif

	return flags;
}

uint32_t rk3506_rpmsg_platform_debug_status(void)
{
	return ((mailbox_last_status & 0xffU) << 24) |
	       ((mailbox_last_cmd & 0xffU) << 16) |
	       (mailbox_last_data & 0xffffU);
}

uint32_t rk3506_rpmsg_platform_last_status(void)
{
	return mailbox_last_status;
}

uint32_t rk3506_rpmsg_platform_last_cmd(void)
{
	return mailbox_last_cmd;
}

uint32_t rk3506_rpmsg_platform_last_data(void)
{
	return mailbox_last_data;
}

uint32_t rk3506_rpmsg_platform_poll_count(void)
{
	return mailbox_poll_count;
}

uint32_t rk3506_rpmsg_platform_poll_hit_count(void)
{
	return mailbox_poll_hit_count;
}

uint32_t rk3506_rpmsg_platform_status_seen_mask(void)
{
	return mailbox_status_seen_mask;
}

uint32_t rk3506_rpmsg_platform_status_seen_count(void)
{
	return mailbox_status_seen_count;
}

uint32_t rk3506_rpmsg_platform_irq_configured(void)
{
	return mailbox_irq_configured;
}

uint32_t rk3506_rpmsg_platform_irq_number(void)
{
#ifdef CONFIG_SOC_RK3506_RPMSG_MBOX_IRQ
	return RK3506_RPMSG_MBOX_BB2_IRQ;
#else
	return 0xffffffffU;
#endif
}

uint32_t rk3506_rpmsg_platform_gic_target_before(void)
{
	return mailbox_gic_target_before;
}

uint32_t rk3506_rpmsg_platform_gic_target_after(void)
{
#ifdef CONFIG_GIC_V2
	return sys_read8(GICD_ITARGETSRn + RK3506_RPMSG_MBOX_BB2_IRQ);
#else
	return mailbox_gic_target_after;
#endif
}

uint32_t rk3506_rpmsg_platform_gic_enable(void)
{
	return rk3506_rpmsg_platform_gic_read_bit(GICD_ISENABLERn);
}

uint32_t rk3506_rpmsg_platform_gic_pending(void)
{
	return rk3506_rpmsg_platform_gic_read_bit(GICD_ISPENDRn);
}

uint32_t rk3506_rpmsg_platform_gic_active(void)
{
#ifdef CONFIG_GIC_V2
	return rk3506_rpmsg_platform_gic_read_bit(GICD_ISACTIVERn);
#else
	return RK3506_GICV2_BAD_VALUE;
#endif
}

uint32_t rk3506_rpmsg_platform_gic_priority(void)
{
#ifdef CONFIG_GIC_V2
	return sys_read8(GICD_IPRIORITYRn + RK3506_RPMSG_MBOX_BB2_IRQ);
#else
	return RK3506_GICV2_BAD_VALUE;
#endif
}

uint32_t rk3506_rpmsg_platform_gic_dist_ctlr(void)
{
#ifdef CONFIG_GIC_V2
	return sys_read32(GICD_CTLR);
#else
	return RK3506_GICV2_BAD_VALUE;
#endif
}

uint32_t rk3506_rpmsg_platform_gic_cpu_ctlr(void)
{
#ifdef CONFIG_GIC_V2
	return sys_read32(GICC_CTLR);
#else
	return RK3506_GICV2_BAD_VALUE;
#endif
}

uint32_t rk3506_rpmsg_platform_gic_cpu_pmr(void)
{
#ifdef CONFIG_GIC_V2
	return sys_read32(GICC_PMR);
#else
	return RK3506_GICV2_BAD_VALUE;
#endif
}

uint32_t rk3506_rpmsg_platform_gic_group(void)
{
#ifdef CONFIG_GIC_V2
	uint32_t irq = RK3506_RPMSG_MBOX_BB2_IRQ;

	return (sys_read32(gic_irq_group_word_addr(irq)) & gic_irq_bit(irq)) != 0U;
#else
	return RK3506_GICV2_BAD_VALUE;
#endif
}

uint32_t rk3506_rpmsg_platform_gic_group_word(void)
{
#ifdef CONFIG_GIC_V2
	return gic_irq_word(GICD_IGROUPRn, RK3506_RPMSG_MBOX_BB2_IRQ);
#else
	return RK3506_GICV2_BAD_VALUE;
#endif
}

uint32_t rk3506_rpmsg_platform_gic_icfgr(void)
{
#ifdef CONFIG_GIC_V2
	uint32_t irq = RK3506_RPMSG_MBOX_BB2_IRQ;
	uint32_t shift = (irq % 16U) * 2U;

	return (sys_read32(gic_irq_icfgr_word_addr(irq)) >> shift) & 0x3U;
#else
	return RK3506_GICV2_BAD_VALUE;
#endif
}

uint32_t rk3506_rpmsg_platform_gic_icfgr_word(void)
{
#ifdef CONFIG_GIC_V2
	return sys_read32(gic_irq_icfgr_word_addr(RK3506_RPMSG_MBOX_BB2_IRQ));
#else
	return RK3506_GICV2_BAD_VALUE;
#endif
}

uint32_t rk3506_rpmsg_platform_gic_enable_word(void)
{
#ifdef CONFIG_GIC_V2
	return gic_irq_word(GICD_ISENABLERn, RK3506_RPMSG_MBOX_BB2_IRQ);
#else
	return RK3506_GICV2_BAD_VALUE;
#endif
}

uint32_t rk3506_rpmsg_platform_gic_pending_word(void)
{
#ifdef CONFIG_GIC_V2
	return gic_irq_word(GICD_ISPENDRn, RK3506_RPMSG_MBOX_BB2_IRQ);
#else
	return RK3506_GICV2_BAD_VALUE;
#endif
}

uint32_t rk3506_rpmsg_platform_gic_active_word(void)
{
#ifdef CONFIG_GIC_V2
	return gic_irq_word(GICD_ISACTIVERn, RK3506_RPMSG_MBOX_BB2_IRQ);
#else
	return RK3506_GICV2_BAD_VALUE;
#endif
}

uint32_t rk3506_rpmsg_platform_gic_group_before(void)
{
	return mailbox_gic_group_before;
}

uint32_t rk3506_rpmsg_platform_gic_group_value(void)
{
	return mailbox_gic_group_value;
}

uint32_t rk3506_rpmsg_platform_gic_group_after(void)
{
	return mailbox_gic_group_after;
}

uint32_t rk3506_rpmsg_platform_gic_enable_before(void)
{
	return mailbox_gic_enable_before;
}

uint32_t rk3506_rpmsg_platform_gic_enable_value(void)
{
	return mailbox_gic_enable_value;
}

uint32_t rk3506_rpmsg_platform_gic_enable_after(void)
{
	return mailbox_gic_enable_after;
}

uint32_t rk3506_rpmsg_platform_mbox2_a2b_inten(void)
{
	return mmio_read32(RK3506_MBOX2_BASE + RK3506_MBOX_A2B_INTEN);
}

uint32_t rk3506_rpmsg_platform_mbox2_a2b_status(void)
{
	return mmio_read32(RK3506_MBOX2_BASE + RK3506_MBOX_A2B_STATUS);
}

uint32_t rk3506_rpmsg_platform_mbox2_a2b_cmd(void)
{
	return mmio_read32(RK3506_MBOX2_BASE + RK3506_MBOX_A2B_CMD);
}

uint32_t rk3506_rpmsg_platform_mbox2_a2b_data(void)
{
	return mmio_read32(RK3506_MBOX2_BASE + RK3506_MBOX_A2B_DATA);
}

uint32_t rk3506_rpmsg_platform_mbox2_b2a_inten(void)
{
	return mmio_read32(RK3506_MBOX2_BASE + RK3506_MBOX_B2A_INTEN);
}

uint32_t rk3506_rpmsg_platform_mbox2_b2a_status(void)
{
	return mmio_read32(RK3506_MBOX2_BASE + RK3506_MBOX_B2A_STATUS);
}

uint32_t rk3506_rpmsg_platform_mbox2_b2a_cmd(void)
{
	return mmio_read32(RK3506_MBOX2_BASE + RK3506_MBOX_B2A_CMD);
}

uint32_t rk3506_rpmsg_platform_mbox2_b2a_data(void)
{
	return mmio_read32(RK3506_MBOX2_BASE + RK3506_MBOX_B2A_DATA);
}

int32_t platform_init_interrupt(uint32_t vector_id, void *isr_data)
{
	env_register_isr(vector_id, isr_data);
	return 0;
}

int32_t platform_deinit_interrupt(uint32_t vector_id)
{
	env_unregister_isr(vector_id);
	return 0;
}

int32_t platform_interrupt_enable(uint32_t vector_id)
{
#ifdef CONFIG_SOC_RK3506_RPMSG_MBOX_IRQ
	mailbox_irq_enable_count++;
	mailbox_irq_last_enable_vector = vector_id;
	rk3506_rpmsg_platform_enable_physical_irq(RK3506_RPMSG_MBOX_BB2_IRQ);
#endif
	return (int32_t)vector_id;
}

int32_t platform_interrupt_disable(uint32_t vector_id)
{
#ifdef CONFIG_SOC_RK3506_RPMSG_MBOX_IRQ
	mailbox_irq_disable_count++;
	mailbox_irq_last_disable_vector = vector_id;
	/*
	 * RPMsg-Lite exposes two virtual queue interrupt IDs, but RK3506 has
	 * one shared physical mailbox IRQ for both directions. Disabling one
	 * virtual queue must not mask the shared physical IRQ.
	 */
	rk3506_rpmsg_platform_enable_physical_irq(RK3506_RPMSG_MBOX_BB2_IRQ);
#endif
	return (int32_t)vector_id;
}

int32_t platform_in_isr(void)
{
	return (int32_t)k_is_in_isr();
}

void platform_notify(uint32_t vector_id)
{
	uint32_t link_id = RL_GET_LINK_ID(vector_id);
	uint32_t qid = RL_GET_Q_ID(vector_id);
	uint32_t base = (qid != 0U) ? RK3506_MBOX2_BASE : RK3506_MBOX0_BASE;

	if (link_id == 0U) {
		link_id = RK3506_RPMSG_LINK_ID;
	}

	(void)mbox_send_b2a(base, link_id);
}

void platform_time_delay(uint32_t num_msec)
{
	k_msleep((int32_t)num_msec);
}

void platform_map_mem_region(uint32_t vrt_addr, uint32_t phy_addr,
			     uint32_t size, uint32_t flags)
{
	ARG_UNUSED(vrt_addr);
	ARG_UNUSED(phy_addr);
	ARG_UNUSED(size);
	ARG_UNUSED(flags);
}

void platform_cache_all_flush_invalidate(void)
{
	barrier_dmem_fence_full();
}

void platform_cache_disable(void)
{
}

uint32_t platform_vatopa(void *addr)
{
	return (uint32_t)(uintptr_t)addr;
}

void *platform_patova(uint32_t addr)
{
	return (void *)(uintptr_t)addr;
}

int32_t platform_init(void)
{
	mbox_enable_a2b_rx(RK3506_MBOX2_BASE);
	mbox_enable_a2b_rx(RK3506_MBOX0_BASE);
	mbox_setup_b2a_tx(RK3506_MBOX2_BASE);
	mbox_setup_b2a_tx(RK3506_MBOX0_BASE);

#ifdef CONFIG_SOC_RK3506_RPMSG_MBOX_IRQ
	IRQ_CONNECT(RK3506_RPMSG_MBOX_BB2_IRQ, RK3506_RPMSG_MBOX_IRQ_PRIORITY,
		    rk3506_rpmsg_mbox_isr, NULL, 0);
	rk3506_rpmsg_platform_route_irq(RK3506_RPMSG_MBOX_BB2_IRQ);
	rk3506_rpmsg_platform_group_irq(RK3506_RPMSG_MBOX_BB2_IRQ);
	rk3506_rpmsg_platform_clear_irq_state(RK3506_RPMSG_MBOX_BB2_IRQ);
	rk3506_rpmsg_platform_enable_physical_irq(RK3506_RPMSG_MBOX_BB2_IRQ);
	mailbox_irq_configured = 1U;
#else
	mailbox_irq_configured = 0U;
#endif

	if (env_create_mutex(&platform_lock, 1) != 0) {
		return -1;
	}

	return 0;
}

int32_t platform_deinit(void)
{
	env_delete_mutex(platform_lock);
	platform_lock = NULL;
	return 0;
}
