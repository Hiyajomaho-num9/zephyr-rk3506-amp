/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT rockchip_rk3506_cru_clock

#include <errno.h>

#include <zephyr/arch/cpu.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/dt-bindings/clock/rk3506-cru.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define RK3506_CRU_CLKSEL_CON35 0x38cU
#define RK3506_CRU_CLKSEL_CON36 0x390U
#define RK3506_CRU_CLKSEL_CON34 0x388U
#define RK3506_CRU_CLKGATE_CON13 0x834U
#define RK3506_CRU_CLKGATE_CON03 0x80cU
#define RK3506_CRU_CLKGATE_CON12 0x830U
#define RK3506_CRU_CLKGATE_CON19 0x84cU
#define RK3506_CRU_CLKGATE_CON22 0x858U
#define RK3506_CRU_PMU_GATE_CON00 0x800U

#define RK3506_CAN0_MUX_SHIFT 11U
#define RK3506_CAN0_MUX_MASK GENMASK(13, 11)
#define RK3506_CAN0_DIV_SHIFT 6U
#define RK3506_CAN0_DIV_MASK GENMASK(10, 6)
#define RK3506_CAN1_MUX_SHIFT 5U
#define RK3506_CAN1_MUX_MASK GENMASK(7, 5)
#define RK3506_CAN1_DIV_SHIFT 0U
#define RK3506_CAN1_DIV_MASK GENMASK(4, 0)
#define RK3506_CAN_CLK_GPLL_MUX 1U
#define RK3506_CAN_CLK_300M_DIV 4U

#define DEV_CFG(dev) ((const struct rk3506_cru_clock_config *)(dev)->config)
#define DEV_DATA(dev) ((struct rk3506_cru_clock_data *)(dev)->data)

struct rk3506_cru_clock_config {
	DEVICE_MMIO_NAMED_ROM(cru);
	DEVICE_MMIO_NAMED_ROM(pmu);
};

struct rk3506_cru_clock_data {
	DEVICE_MMIO_NAMED_RAM(cru);
	DEVICE_MMIO_NAMED_RAM(pmu);
};

static inline void rk3506_cru_write_mask(uintptr_t base, uint32_t offset,
					 uint32_t mask, uint32_t value)
{
	sys_write32((mask << 16) | (value & mask), base + offset);
}

static int rk3506_cru_clock_set_can_rate(const struct device *dev, uint32_t id,
					 uint32_t rate)
{
	uintptr_t base = DEVICE_MMIO_NAMED_GET(dev, cru);
	uint32_t clksel;
	uint32_t mux_mask;
	uint32_t mux_value;
	uint32_t div_mask;
	uint32_t div_value;

	if (rate != 300000000U) {
		return -ENOTSUP;
	}

	switch (id) {
	case CLK_CAN0:
		clksel = RK3506_CRU_CLKSEL_CON35;
		mux_mask = RK3506_CAN0_MUX_MASK;
		mux_value = RK3506_CAN_CLK_GPLL_MUX << RK3506_CAN0_MUX_SHIFT;
		div_mask = RK3506_CAN0_DIV_MASK;
		div_value = RK3506_CAN_CLK_300M_DIV << RK3506_CAN0_DIV_SHIFT;
		break;
	case CLK_CAN1:
		clksel = RK3506_CRU_CLKSEL_CON36;
		mux_mask = RK3506_CAN1_MUX_MASK;
		mux_value = RK3506_CAN_CLK_GPLL_MUX << RK3506_CAN1_MUX_SHIFT;
		div_mask = RK3506_CAN1_DIV_MASK;
		div_value = RK3506_CAN_CLK_300M_DIV << RK3506_CAN1_DIV_SHIFT;
		break;
	default:
		return -ENOTSUP;
	}

	rk3506_cru_write_mask(base, clksel, mux_mask, mux_value);
	rk3506_cru_write_mask(base, clksel, div_mask, div_value);

	return 0;
}

static int rk3506_cru_clock_set_spi_rate(const struct device *dev, uint32_t id,
					 uint32_t rate)
{
	uint32_t mask;

	if (rate != 24000000U) {
		return -ENOTSUP;
	}

	/* OSC 24 MHz, divider 1. Avoid assuming any PLL is available to CPU2. */
	if (id == CLK_SPI0) {
		mask = GENMASK(9, 4);
	} else if (id == CLK_SPI1) {
		mask = GENMASK(15, 10);
	} else {
		return -ENOTSUP;
	}

	rk3506_cru_write_mask(DEVICE_MMIO_NAMED_GET(dev, cru),
			      RK3506_CRU_CLKSEL_CON34, mask, 0U);
	return 0;
}

/*
 * Gate bits read 1 when the clock is off. GPIO pclk gates live in different
 * gate registers:
 *   gpio0 -> CRU_PMU_GATE_CON00.bit8 (PMU CRU @ 0xff9b0000)
 *   gpio1 -> CRU_GATE_CON03.bit8
 *   gpio2 -> CRU_GATE_CON12.bit14
 *   gpio3 -> CRU_GATE_CON13.bit0
 *   gpio4 -> CRU_GATE_CON13.bit2
 */
static const struct rk3506_cru_gate {
	uint16_t id;
	uint16_t offset;
	uint8_t bit;
	bool pmu;
} rk3506_cru_gates[] = {
	{HCLK_CAN0, RK3506_CRU_CLKGATE_CON13, 4U, false},
	{CLK_CAN0, RK3506_CRU_CLKGATE_CON13, 5U, false},
	{HCLK_CAN1, RK3506_CRU_CLKGATE_CON13, 6U, false},
	{CLK_CAN1, RK3506_CRU_CLKGATE_CON13, 7U, false},
	{HCLK_GPIO0, RK3506_CRU_PMU_GATE_CON00, 8U, true},
	{HCLK_GPIO1, RK3506_CRU_CLKGATE_CON03, 8U, false},
	{HCLK_GPIO2, RK3506_CRU_CLKGATE_CON12, 14U, false},
	{HCLK_GPIO3, RK3506_CRU_CLKGATE_CON13, 0U, false},
	{HCLK_GPIO4, RK3506_CRU_CLKGATE_CON13, 2U, false},
	{PCLK_GPIO0_IOC, RK3506_CRU_PMU_GATE_CON00, 7U, true},
	{PCLK_GPIO1_IOC, RK3506_CRU_CLKGATE_CON22, 1U, false},
	{PCLK_GPIO234_IOC, RK3506_CRU_CLKGATE_CON19, 8U, false},
	{PCLK_SPI0, RK3506_CRU_CLKGATE_CON12, 10U, false},
	{CLK_SPI0, RK3506_CRU_CLKGATE_CON12, 11U, false},
	{PCLK_SPI1, RK3506_CRU_CLKGATE_CON12, 12U, false},
	{CLK_SPI1, RK3506_CRU_CLKGATE_CON12, 13U, false},
};

static const struct rk3506_cru_gate *rk3506_cru_find_gate(clock_control_subsys_t sys)
{
	uint32_t id = (uint32_t)(uintptr_t)sys;

	ARRAY_FOR_EACH(rk3506_cru_gates, i) {
		if (rk3506_cru_gates[i].id == id) {
			return &rk3506_cru_gates[i];
		}
	}

	return NULL;
}

static uintptr_t rk3506_cru_gate_base(const struct device *dev,
				      const struct rk3506_cru_gate *gate)
{
	return gate->pmu ? DEVICE_MMIO_NAMED_GET(dev, pmu) :
			   DEVICE_MMIO_NAMED_GET(dev, cru);
}

static int rk3506_cru_clock_on(const struct device *dev, clock_control_subsys_t sys)
{
	const struct rk3506_cru_gate *gate = rk3506_cru_find_gate(sys);

	if (gate == NULL) {
		return -ENOTSUP;
	}

	rk3506_cru_write_mask(rk3506_cru_gate_base(dev, gate), gate->offset,
			      BIT(gate->bit), 0U);

	return 0;
}

/*
 * Lets CPU2 drivers notice that Linux gated one of their clocks, e.g. from
 * clk_disable_unused() when the Linux AMP node does not list it.
 */
static enum clock_control_status rk3506_cru_clock_get_status(const struct device *dev,
							     clock_control_subsys_t sys)
{
	const struct rk3506_cru_gate *gate = rk3506_cru_find_gate(sys);
	uint32_t value;

	if (gate == NULL) {
		return CLOCK_CONTROL_STATUS_UNKNOWN;
	}

	value = sys_read32(rk3506_cru_gate_base(dev, gate) + gate->offset);

	return ((value & BIT(gate->bit)) != 0U) ? CLOCK_CONTROL_STATUS_OFF :
						  CLOCK_CONTROL_STATUS_ON;
}

static int rk3506_cru_clock_get_rate(const struct device *dev,
				     clock_control_subsys_t sys,
				     uint32_t *rate)
{
	uint32_t id = (uint32_t)(uintptr_t)sys;

	switch (id) {
	case CLK_CAN0:
	case CLK_CAN1:
		*rate = 300000000U;
		return 0;
	case CLK_SPI0:
	case CLK_SPI1:
		*rate = 24000000U;
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int rk3506_cru_clock_set_rate(const struct device *dev,
				     clock_control_subsys_t sys,
				     clock_control_subsys_rate_t rate)
{
	uint32_t id = (uint32_t)(uintptr_t)sys;
	uint32_t hz = *(uint32_t *)rate;

	if (id == CLK_SPI0 || id == CLK_SPI1) {
		return rk3506_cru_clock_set_spi_rate(dev, id, hz);
	}

	return rk3506_cru_clock_set_can_rate(dev, id, hz);
}

static int rk3506_cru_clock_init(const struct device *dev)
{
	DEVICE_MMIO_NAMED_MAP(dev, cru, K_MEM_CACHE_NONE);
	DEVICE_MMIO_NAMED_MAP(dev, pmu, K_MEM_CACHE_NONE);

	return 0;
}

static DEVICE_API(clock_control, rk3506_cru_clock_api) = {
	.on = rk3506_cru_clock_on,
	.get_status = rk3506_cru_clock_get_status,
	.get_rate = rk3506_cru_clock_get_rate,
	.set_rate = rk3506_cru_clock_set_rate,
};

static const struct rk3506_cru_clock_config rk3506_cru_clock_config = {
	DEVICE_MMIO_NAMED_ROM_INIT_BY_NAME(cru, DT_DRV_INST(0)),
	DEVICE_MMIO_NAMED_ROM_INIT_BY_NAME(pmu, DT_DRV_INST(0)),
};

static struct rk3506_cru_clock_data rk3506_cru_clock_data;

DEVICE_DT_INST_DEFINE(0, rk3506_cru_clock_init, NULL, &rk3506_cru_clock_data,
		      &rk3506_cru_clock_config, PRE_KERNEL_1,
		      CONFIG_CLOCK_CONTROL_INIT_PRIORITY, &rk3506_cru_clock_api);
