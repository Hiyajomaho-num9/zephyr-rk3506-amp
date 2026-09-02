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
#define RK3506_CRU_CLKGATE_CON13 0x834U
#define RK3506_CRU_CLKGATE_CON03 0x80cU
#define RK3506_CRU_CLKGATE_CON12 0x830U
#define RK3506_CRU_PMU_BASE      0xff9b0000U
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

struct rk3506_cru_clock_config {
	uintptr_t base;
};

static inline void rk3506_cru_write_mask(uintptr_t base, uint32_t offset,
					 uint32_t mask, uint32_t value)
{
	sys_write32((mask << 16) | (value & mask), base + offset);
}

static int rk3506_cru_clock_set_can_rate(const struct device *dev, uint32_t id,
					 uint32_t rate)
{
	const struct rk3506_cru_clock_config *config = dev->config;
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

	rk3506_cru_write_mask(config->base, clksel, mux_mask, mux_value);
	rk3506_cru_write_mask(config->base, clksel, div_mask, div_value);

	return 0;
}

static int rk3506_cru_clock_on(const struct device *dev, clock_control_subsys_t sys)
{
	const struct rk3506_cru_clock_config *config = dev->config;
	uint32_t id = (uint32_t)(uintptr_t)sys;
	uint32_t gate_bit;

	switch (id) {
	case HCLK_CAN0:
		gate_bit = 4U;
		break;
	case CLK_CAN0:
		gate_bit = 5U;
		break;
	case HCLK_CAN1:
		gate_bit = 6U;
		break;
	case CLK_CAN1:
		gate_bit = 7U;
		break;
	/*
	 * GPIO pclk gates live in different gate registers:
	 *   gpio0 -> CRU_PMU_GATE_CON00.bit8 (PMU CRU @ 0xff9b0000)
	 *   gpio1 -> CRU_GATE_CON03.bit8
	 *   gpio2 -> CRU_GATE_CON12.bit14
	 *   gpio3 -> CRU_GATE_CON13.bit0
	 *   gpio4 -> CRU_GATE_CON13.bit2
	 */
	case HCLK_GPIO0:
		rk3506_cru_write_mask(RK3506_CRU_PMU_BASE,
				      RK3506_CRU_PMU_GATE_CON00, BIT(8), 0U);
		return 0;
	case HCLK_GPIO1:
		rk3506_cru_write_mask(config->base, RK3506_CRU_CLKGATE_CON03,
				      BIT(8), 0U);
		return 0;
	case HCLK_GPIO2:
		rk3506_cru_write_mask(config->base, RK3506_CRU_CLKGATE_CON12,
				      BIT(14), 0U);
		return 0;
	case HCLK_GPIO3:
		rk3506_cru_write_mask(config->base, RK3506_CRU_CLKGATE_CON13,
				      BIT(0), 0U);
		return 0;
	case HCLK_GPIO4:
		rk3506_cru_write_mask(config->base, RK3506_CRU_CLKGATE_CON13,
				      BIT(2), 0U);
		return 0;
	default:
		return -ENOTSUP;
	}

	rk3506_cru_write_mask(config->base, RK3506_CRU_CLKGATE_CON13, BIT(gate_bit), 0U);

	return 0;
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

	return rk3506_cru_clock_set_can_rate(dev, id, hz);
}

static int rk3506_cru_clock_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

static DEVICE_API(clock_control, rk3506_cru_clock_api) = {
	.on = rk3506_cru_clock_on,
	.get_rate = rk3506_cru_clock_get_rate,
	.set_rate = rk3506_cru_clock_set_rate,
};

static const struct rk3506_cru_clock_config rk3506_cru_clock_config = {
	.base = DT_INST_REG_ADDR(0),
};

DEVICE_DT_INST_DEFINE(0, rk3506_cru_clock_init, NULL, NULL,
		      &rk3506_cru_clock_config, PRE_KERNEL_1,
		      CONFIG_CLOCK_CONTROL_INIT_PRIORITY, &rk3506_cru_clock_api);
