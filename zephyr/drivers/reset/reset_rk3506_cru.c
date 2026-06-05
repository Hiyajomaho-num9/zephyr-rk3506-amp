/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT rockchip_rk3506_cru_reset

#include <errno.h>

#include <zephyr/arch/cpu.h>
#include <zephyr/device.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/dt-bindings/reset/rk3506-cru.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define RK3506_CRU_SOFTRST_CON13 0xa34U

struct rk3506_cru_reset_config {
	uintptr_t base;
};

static int rk3506_cru_reset_bit(uint32_t id, uint32_t *bit)
{
	switch (id) {
	case SRST_H_CAN0:
		*bit = 4U;
		return 0;
	case SRST_CAN0:
		*bit = 5U;
		return 0;
	case SRST_H_CAN1:
		*bit = 6U;
		return 0;
	case SRST_CAN1:
		*bit = 7U;
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int rk3506_cru_reset_set(const struct device *dev, uint32_t id, bool assert)
{
	const struct rk3506_cru_reset_config *config = dev->config;
	uint32_t bit;
	int ret;

	ret = rk3506_cru_reset_bit(id, &bit);
	if (ret != 0) {
		return ret;
	}

	sys_write32((BIT(bit) << 16) | (assert ? BIT(bit) : 0U),
		    config->base + RK3506_CRU_SOFTRST_CON13);

	return 0;
}

static int rk3506_cru_reset_line_assert(const struct device *dev, uint32_t id)
{
	return rk3506_cru_reset_set(dev, id, true);
}

static int rk3506_cru_reset_line_deassert(const struct device *dev, uint32_t id)
{
	return rk3506_cru_reset_set(dev, id, false);
}

static int rk3506_cru_reset_line_toggle(const struct device *dev, uint32_t id)
{
	int ret;

	ret = rk3506_cru_reset_line_assert(dev, id);
	if (ret != 0) {
		return ret;
	}

	k_busy_wait(10U);

	return rk3506_cru_reset_line_deassert(dev, id);
}

static int rk3506_cru_reset_init(const struct device *dev)
{
	ARG_UNUSED(dev);

	return 0;
}

static DEVICE_API(reset, rk3506_cru_reset_api) = {
	.line_assert = rk3506_cru_reset_line_assert,
	.line_deassert = rk3506_cru_reset_line_deassert,
	.line_toggle = rk3506_cru_reset_line_toggle,
};

static const struct rk3506_cru_reset_config rk3506_cru_reset_config = {
	.base = DT_REG_ADDR(DT_INST_PHANDLE(0, cru)),
};

DEVICE_DT_INST_DEFINE(0, rk3506_cru_reset_init, NULL, NULL,
		      &rk3506_cru_reset_config, PRE_KERNEL_1,
		      CONFIG_RESET_INIT_PRIORITY, &rk3506_cru_reset_api);
