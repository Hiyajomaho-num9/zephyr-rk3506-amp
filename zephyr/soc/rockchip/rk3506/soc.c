/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/arch/arm/mmu/arm_mmu.h>
#include <zephyr/devicetree.h>
#include <zephyr/linker/linker-defs.h>
#include <zephyr/sys/util.h>

#include <cmsis_core.h>

#include "rk3506_amp_layout.h"
#include "soc.h"

#define RK3506_UART1_BASE 0xff0b0000U
#define RK3506_CRU_BASE 0xff9a0000U
#define RK3506_RM_IO_BASE 0xff910000U
#define RK3506_GPIO0_IOC_BASE 0xff950000U
#define RK3506_MAILBOX_BASE 0xff290000U
#define RK3506_CAN0_BASE 0xff320000U
#define RK3506_CAN1_BASE 0xff330000U

#define RK3506_CRU_GATE_CON6 (RK3506_CRU_BASE + 0x818U)
#define RK3506_CRU_GATE_CON11 (RK3506_CRU_BASE + 0x82cU)
#define RK3506_CRU_CLKSEL_CON30 (RK3506_CRU_BASE + 0x378U)
#define RK3506_GPIO0A_IOMUX_SEL_0 (RK3506_GPIO0_IOC_BASE + 0x000U)
#define RK3506_RM_GPIO0A0_SEL (RK3506_RM_IO_BASE + 0x080U)
#define RK3506_RM_GPIO0A1_SEL (RK3506_RM_IO_BASE + 0x084U)

#define RK3506_UART_THR 0x00U
#define RK3506_UART_DLL 0x00U
#define RK3506_UART_DLH 0x04U
#define RK3506_UART_IER 0x04U
#define RK3506_UART_FCR 0x08U
#define RK3506_UART_LCR 0x0cU
#define RK3506_UART_MCR 0x10U
#define RK3506_UART_LSR 0x14U
#define RK3506_UART_USR 0x7cU
#define RK3506_UART_LSR_THRE BIT(5)
#define RK3506_UART_USR_TX_FIFO_NOT_FULL BIT(1)
#define RK3506_UART_LCR_DLAB BIT(7)
#define RK3506_UART_LCR_8N1 0x03U
#define RK3506_UART_MCR_LOOP BIT(4)
#define RK3506_UART_FCR_ENABLE_CLEAR 0x07U
#define RK3506_UART_CLOCK_HZ 24000000U
#define RK3506_UART_BAUD 1500000U
#define RK3506_MAILBOX_PCLK_GATE_SHIFT 13U
#define RK3506_UART1_PCLK_GATE_SHIFT 5U
#define RK3506_UART1_SCLK_GATE_SHIFT 10U
#define RK3506_VBAR_MASK 0xffffffe0U
#define RK3506_SCTLR_HIGH_VECTORS BIT(13)
#define RK3506_AMP_SHARED_MMU_ATTRS \
	(MT_STRONGLY_ORDERED | MPERM_R | MPERM_W | MATTR_MAY_MAP_L1_SECTION)
#define RK3506_NORMAL_CODE_MMU_ATTRS \
	(MT_NORMAL | MATTR_SHARED | MPERM_R | MPERM_X | \
	 MATTR_CACHE_OUTER_WB_nWA | MATTR_CACHE_INNER_WB_nWA | \
	 MATTR_MAY_MAP_L1_SECTION)
#define RK3506_NORMAL_DATA_MMU_ATTRS \
	(MT_NORMAL | MATTR_SHARED | MPERM_R | MPERM_W | \
	 MATTR_CACHE_OUTER_WB_WA | MATTR_CACHE_INNER_WB_WA)
#define RK3506_NORMAL_RODATA_MMU_ATTRS \
	(MT_NORMAL | MATTR_SHARED | MPERM_R | \
	 MATTR_CACHE_OUTER_WB_nWA | MATTR_CACHE_INNER_WB_nWA | \
	 MATTR_MAY_MAP_L1_SECTION)

static void rk3506_set_vector_base(void)
{
	uint32_t sctlr = __get_SCTLR();

	sctlr &= ~RK3506_SCTLR_HIGH_VECTORS;
	__set_SCTLR(sctlr);
	__ISB();

	__set_VBAR(((uint32_t)_vector_start) & RK3506_VBAR_MASK);
	__ISB();
}

static uint32_t rk3506_mmio_read32(uint32_t addr)
{
	return *(volatile uint32_t *)addr;
}

static void rk3506_mmio_write32(uint32_t val, uint32_t addr)
{
	*(volatile uint32_t *)addr = val;
}

static void rk3506_mmio_write_mask32(uint32_t addr, uint32_t mask, uint32_t val)
{
	rk3506_mmio_write32((mask << 16) | (val & mask), addr);
}

static void rk3506_uart1_putc(char c)
{
	while ((rk3506_mmio_read32(RK3506_UART1_BASE + RK3506_UART_USR) &
		RK3506_UART_USR_TX_FIFO_NOT_FULL) == 0U) {
	}

	rk3506_mmio_write32((uint32_t)c, RK3506_UART1_BASE + RK3506_UART_THR);
}

static void rk3506_uart1_puts(const char *s)
{
	while (*s != '\0') {
		if (*s == '\n') {
			rk3506_uart1_putc('\r');
		}
		rk3506_uart1_putc(*s++);
	}
}

static void rk3506_uart1_clock_init(void)
{
	rk3506_mmio_write_mask32(RK3506_CRU_GATE_CON6,
				 BIT(RK3506_MAILBOX_PCLK_GATE_SHIFT), 0U);
	rk3506_mmio_write_mask32(RK3506_CRU_CLKSEL_CON30, 0xffU, 0U);
	rk3506_mmio_write_mask32(RK3506_CRU_GATE_CON11,
				 BIT(RK3506_UART1_PCLK_GATE_SHIFT), 0U);
	rk3506_mmio_write_mask32(RK3506_CRU_GATE_CON11,
				 BIT(RK3506_UART1_SCLK_GATE_SHIFT), 0U);
}

static void rk3506_uart1_rmio_init(void)
{
	/*
	 * Linux AMP DTS routes UART1 as:
	 * TX = rm_io0_uart1_tx, RX = rm_io1_uart1_rx.
	 */
	rk3506_mmio_write_mask32(RK3506_GPIO0A_IOMUX_SEL_0, 0x000fU, 0x0007U);
	rk3506_mmio_write_mask32(RK3506_GPIO0A_IOMUX_SEL_0, 0x00f0U, 0x0070U);
	rk3506_mmio_write_mask32(RK3506_RM_GPIO0A0_SEL, 0x007fU, 0x0001U);
	rk3506_mmio_write_mask32(RK3506_RM_GPIO0A1_SEL, 0x007fU, 0x0002U);
}

static void rk3506_uart1_init_early(void)
{
	const uint32_t divisor = RK3506_UART_CLOCK_HZ / (16U * RK3506_UART_BAUD);

	rk3506_mmio_write32(0U, RK3506_UART1_BASE + RK3506_UART_IER);
	rk3506_mmio_write32(RK3506_UART_MCR_LOOP, RK3506_UART1_BASE + RK3506_UART_MCR);
	rk3506_mmio_write32(RK3506_UART_LCR_DLAB, RK3506_UART1_BASE + RK3506_UART_LCR);
	rk3506_mmio_write32(divisor & 0xffU, RK3506_UART1_BASE + RK3506_UART_DLL);
	rk3506_mmio_write32((divisor >> 8) & 0xffU, RK3506_UART1_BASE + RK3506_UART_DLH);
	rk3506_mmio_write32(RK3506_UART_LCR_8N1, RK3506_UART1_BASE + RK3506_UART_LCR);
	rk3506_mmio_write32(RK3506_UART_FCR_ENABLE_CLEAR, RK3506_UART1_BASE + RK3506_UART_FCR);
	rk3506_mmio_write32(0U, RK3506_UART1_BASE + RK3506_UART_MCR);
}

static void rk3506_apply_cache_policy(void)
{
	uint32_t sctlr = __get_SCTLR();
	const uint32_t old_sctlr = sctlr;

#if defined(CONFIG_SOC_RK3506_PRIVATE_ICACHE)
	if ((sctlr & SCTLR_C_Msk) != 0U) {
		L1C_CleanInvalidateDCacheAll();
	}
	L1C_InvalidateICacheAll();
	sctlr |= SCTLR_I_Msk;
#else
	sctlr &= ~SCTLR_I_Msk;
#endif
#if defined(CONFIG_SOC_RK3506_PRIVATE_DCACHE)
	if ((sctlr & SCTLR_M_Msk) == 0U) {
		sctlr &= ~SCTLR_C_Msk;
	} else if ((sctlr & SCTLR_C_Msk) == 0U) {
		L1C_InvalidateDCacheAll();
		sctlr |= SCTLR_C_Msk;
	} else {
		sctlr |= SCTLR_C_Msk;
	}
#else
	sctlr &= ~SCTLR_C_Msk;
#endif
	sctlr &= ~SCTLR_A_Msk;

	/*
	 * Turning a live D-cache off must write its dirty lines back first,
	 * otherwise every store made while it was on is silently lost. The
	 * generic ARMv7 MMU init no longer enables the D-cache on RK3506
	 * (arch/arm/core/mmu/arm_mmu.c), so this only fires if some earlier
	 * stage left the cache on; keep it as a safety net.
	 */
	if (((old_sctlr & SCTLR_C_Msk) != 0U) && ((sctlr & SCTLR_C_Msk) == 0U)) {
		L1C_CleanInvalidateDCacheAll();
	}
	__DSB();
	__set_SCTLR(sctlr);
	__ISB();
}

void soc_early_init_hook(void)
{
	/*
	 * z_arm_mmu_init() runs before z_cstart()/soc_early_init_hook().
	 * Keep the RK3506 AMP policy effective after the generic MMU setup.
	 */
	rk3506_apply_cache_policy();

	rk3506_uart1_clock_init();
	rk3506_uart1_rmio_init();
	rk3506_uart1_init_early();
#if defined(CONFIG_SOC_RK3506_BOOT_BANNER)
	rk3506_uart1_puts("[RK3506][CLEAN-ZEPHYR] soc_early_init_hook\n");
#endif
}

void soc_reset_hook(void)
{
	rk3506_set_vector_base();
	rk3506_apply_cache_policy();
}

void relocate_vector_table(void)
{
	rk3506_set_vector_base();
}

uint32_t rk3506_soc_amp_shmem_mmu_attrs(void)
{
	return RK3506_AMP_SHARED_MMU_ATTRS;
}

uint32_t rk3506_soc_rpmsg_mmu_attrs(void)
{
	return RK3506_AMP_SHARED_MMU_ATTRS;
}

uint32_t rk3506_soc_cache_policy(void)
{
#if defined(CONFIG_SOC_RK3506_PRIVATE_DCACHE)
	return RK3506_AMP_CACHE_POLICY_IDCACHE;
#elif defined(CONFIG_SOC_RK3506_PRIVATE_ICACHE)
	return RK3506_AMP_CACHE_POLICY_ICACHE_ONLY;
#else
	return 0U;
#endif
}

uint32_t rk3506_soc_cache_preflight_policy(void)
{
#if defined(CONFIG_SOC_RK3506_PRIVATE_DCACHE)
	return RK3506_AMP_CACHE_DCACHE_POLICY;
#elif defined(CONFIG_SOC_RK3506_PRIVATE_ICACHE)
	return RK3506_AMP_CACHE_PREFLIGHT_POLICY;
#else
	return 0U;
#endif
}

uint32_t rk3506_soc_sctlr_snapshot(void)
{
	return __get_SCTLR();
}

uint32_t rk3506_soc_ttbr0_snapshot(void)
{
	return __get_TTBR0();
}

uint32_t rk3506_soc_dacr_snapshot(void)
{
	return __get_DACR();
}

uint32_t rk3506_soc_ttbcr_snapshot(void)
{
	uint32_t ttbcr;

	__asm__ volatile("mrc p15, 0, %0, c2, c0, 2" : "=r"(ttbcr));
	return ttbcr;
}

uint32_t rk3506_soc_normal_code_mmu_attrs(void)
{
	return RK3506_NORMAL_CODE_MMU_ATTRS;
}

uint32_t rk3506_soc_normal_data_mmu_attrs(void)
{
	return RK3506_NORMAL_DATA_MMU_ATTRS;
}

uint32_t rk3506_soc_normal_rodata_mmu_attrs(void)
{
	return RK3506_NORMAL_RODATA_MMU_ATTRS;
}

#ifdef CONFIG_ARM_AARCH32_MMU
static const struct arm_mmu_region mmu_regions[] = {
	MMU_REGION_FLAT_ENTRY("vectors",
			      CONFIG_KERNEL_VM_BASE,
			      0x1000,
			      MT_STRONGLY_ORDERED | MPERM_R | MPERM_X),
	MMU_REGION_FLAT_ENTRY("gic",
			      0xff581000,
			      0x6000,
			      MT_STRONGLY_ORDERED | MPERM_R | MPERM_W),
	MMU_REGION_FLAT_ENTRY("rmio",
			      RK3506_RM_IO_BASE,
			      0x1000,
			      MT_DEVICE | MPERM_R | MPERM_W),
	MMU_REGION_FLAT_ENTRY("gpio0-ioc",
			      RK3506_GPIO0_IOC_BASE,
			      0x1000,
			      MT_DEVICE | MPERM_R | MPERM_W),
	MMU_REGION_FLAT_ENTRY("cru",
			      RK3506_CRU_BASE,
			      0x1000,
			      MT_DEVICE | MPERM_R | MPERM_W),
	MMU_REGION_FLAT_ENTRY("uart1",
			      RK3506_UART1_BASE,
			      0x1000,
			      MT_DEVICE | MPERM_R | MPERM_W),
	MMU_REGION_FLAT_ENTRY("mailbox",
			      RK3506_MAILBOX_BASE,
			      0x3000,
			      MT_DEVICE | MPERM_R | MPERM_W),
#if DT_NODE_HAS_STATUS(DT_NODELABEL(can0), okay)
	MMU_REGION_FLAT_ENTRY("can0",
			      RK3506_CAN0_BASE,
			      0x1000,
			      MT_DEVICE | MPERM_R | MPERM_W),
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(can1), okay)
	MMU_REGION_FLAT_ENTRY("can1",
			      RK3506_CAN1_BASE,
			      0x1000,
			      MT_DEVICE | MPERM_R | MPERM_W),
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(spi0), okay)
	MMU_REGION_FLAT_ENTRY("spi0", 0xff120000, 0x1000,
			      MT_DEVICE | MPERM_R | MPERM_W),
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(spi1), okay)
	MMU_REGION_FLAT_ENTRY("spi1", 0xff130000, 0x1000,
			      MT_DEVICE | MPERM_R | MPERM_W),
#endif
	MMU_REGION_FLAT_ENTRY("amp-shmem",
			      RK3506_AMP_SHMEM_BASE,
			      RK3506_AMP_SHMEM_SIZE,
			      RK3506_AMP_SHARED_MMU_ATTRS),
	MMU_REGION_FLAT_ENTRY("rpmsg",
			      RK3506_RPMSG_SHMEM_BASE,
			      RK3506_RPMSG_SHMEM_SIZE,
			      RK3506_AMP_SHARED_MMU_ATTRS),
};

const struct arm_mmu_config mmu_config = {
	.num_regions = ARRAY_SIZE(mmu_regions),
	.mmu_regions = mmu_regions,
};
#endif /* CONFIG_ARM_AARCH32_MMU */
