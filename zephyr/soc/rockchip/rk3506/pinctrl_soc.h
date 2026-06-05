/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_ARM_ROCKCHIP_RK3506_PINCTRL_SOC_H_
#define ZEPHYR_SOC_ARM_ROCKCHIP_RK3506_PINCTRL_SOC_H_

#include <zephyr/devicetree.h>
#include <zephyr/types.h>

typedef struct pinctrl_soc_pin_t {
	uint8_t bank;
	uint8_t pin;
	uint8_t mux;
	uint8_t rmio_func;
} pinctrl_soc_pin_t;

#define RK3506_DT_PIN(node_id)					\
	{							\
		.bank = DT_PROP_BY_IDX(node_id, pinmux, 0),	\
		.pin = DT_PROP_BY_IDX(node_id, pinmux, 1),	\
		.mux = DT_PROP_BY_IDX(node_id, pinmux, 2),	\
		.rmio_func = DT_PROP_BY_IDX(node_id, pinmux, 3),\
	},

#define Z_PINCTRL_STATE_PIN_INIT(node_id, prop, idx)		\
	RK3506_DT_PIN(DT_PROP_BY_IDX(node_id, prop, idx))

#define Z_PINCTRL_STATE_PINS_INIT(node_id, prop)		\
	{ DT_FOREACH_PROP_ELEM(node_id, prop, Z_PINCTRL_STATE_PIN_INIT) }

#endif /* ZEPHYR_SOC_ARM_ROCKCHIP_RK3506_PINCTRL_SOC_H_ */
