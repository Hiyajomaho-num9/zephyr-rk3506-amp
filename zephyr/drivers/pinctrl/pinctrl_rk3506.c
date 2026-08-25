/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT rockchip_rk3506_pinctrl

#include <zephyr/arch/cpu.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define RK3506_GPIO_BANK_WIDTH 8U
#define RK3506_GPIO_MUX_BITS   4U
#define RK3506_RMIO_BITS       7U

#define RK3506_GPIO0_IOMUX_SEL_STRIDE 0x8U
#define RK3506_GPIO0_IOMUX_SEL_LO     0x0U
#define RK3506_GPIO0_IOMUX_SEL_HI     0x4U
#define RK3506_RMIO_SEL_BASE          0x80U
#define RK3506_RMIO_SEL_STRIDE        0x4U

#define RK3506_HIWORD_UPDATE(mask, val) ((((mask) & 0xffffU) << 16) | ((val) & (mask)))

struct rk3506_pinctrl_config {
	uintptr_t gpio0_ioc_base;
	uintptr_t rmio_base;
};

static inline void rk3506_hiword_write(uintptr_t addr, uint32_t mask, uint32_t value)
{
	sys_write32(RK3506_HIWORD_UPDATE(mask, value), addr);
}

static uintptr_t rk3506_gpio_iomux_addr(uintptr_t gpio0_ioc_base, const pinctrl_soc_pin_t *pin,
					uint32_t *shift)
{
	uintptr_t bank_base = gpio0_ioc_base + (pin->bank * RK3506_GPIO0_IOMUX_SEL_STRIDE);

	*shift = (pin->pin % 4U) * RK3506_GPIO_MUX_BITS;
	if (pin->pin >= 4U) {
		return bank_base + RK3506_GPIO0_IOMUX_SEL_HI;
	}

	return bank_base + RK3506_GPIO0_IOMUX_SEL_LO;
}

static uintptr_t rk3506_rmio_addr(uintptr_t rmio_base, const pinctrl_soc_pin_t *pin)
{
	uint32_t rmio = (pin->bank * RK3506_GPIO_BANK_WIDTH) + pin->pin;

	return rmio_base + RK3506_RMIO_SEL_BASE + (rmio * RK3506_RMIO_SEL_STRIDE);
}

int pinctrl_configure_pins(const pinctrl_soc_pin_t *pins, uint8_t pin_cnt, uintptr_t reg)
{
	const struct device *dev = DEVICE_DT_GET(DT_DRV_INST(0));
	const struct rk3506_pinctrl_config *config = dev->config;

	ARG_UNUSED(reg);

	for (uint8_t i = 0U; i < pin_cnt; i++) {
		uint32_t shift;
		uint32_t mux_mask;
		uint32_t rmio_mask = GENMASK(RK3506_RMIO_BITS - 1U, 0U);
		uintptr_t mux_addr = rk3506_gpio_iomux_addr(config->gpio0_ioc_base, &pins[i], &shift);
		uintptr_t rmio_addr = rk3506_rmio_addr(config->rmio_base, &pins[i]);

		mux_mask = GENMASK(shift + RK3506_GPIO_MUX_BITS - 1U, shift);
		rk3506_hiword_write(mux_addr, mux_mask, (uint32_t)pins[i].mux << shift);
		rk3506_hiword_write(rmio_addr, rmio_mask, pins[i].rmio_func);
	}

	return 0;
}

static int rk3506_pinctrl_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

static const struct rk3506_pinctrl_config rk3506_pinctrl_config = {
	.gpio0_ioc_base = DT_INST_REG_ADDR(0),
	.rmio_base = DT_INST_PROP(0, rmio_base),
};

DEVICE_DT_INST_DEFINE(0, rk3506_pinctrl_init, NULL, NULL, &rk3506_pinctrl_config,
		      PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, NULL);
