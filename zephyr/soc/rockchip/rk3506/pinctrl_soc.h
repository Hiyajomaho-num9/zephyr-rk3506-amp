/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_ARM_ROCKCHIP_RK3506_PINCTRL_SOC_H_
#define ZEPHYR_SOC_ARM_ROCKCHIP_RK3506_PINCTRL_SOC_H_

#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/pinctrl/rk3506-pinctrl.h>
#include <zephyr/sys/util.h>
#include <zephyr/types.h>

#define RK3506_PIN_BIAS_DISABLE BIT(0)
#define RK3506_PIN_PULL_UP BIT(1)
#define RK3506_PIN_PULL_DOWN BIT(2)
#define RK3506_PIN_INPUT_ENABLE BIT(3)
#define RK3506_PIN_INPUT_DISABLE BIT(4)
#define RK3506_PIN_SCHMITT_ENABLE BIT(5)
#define RK3506_PIN_SCHMITT_DISABLE BIT(6)
#define RK3506_PIN_OPEN_DRAIN BIT(7)
#define RK3506_PIN_PUSH_PULL BIT(8)
#define RK3506_PIN_DRIVE_LEVEL BIT(9)
#define RK3506_PIN_SLEW_RATE BIT(10)

typedef struct pinctrl_soc_pin_t {
	uint32_t pinmux;
	uint16_t flags;
	uint8_t drive_level;
	uint8_t slew_rate;
} pinctrl_soc_pin_t;

/* Package pad numbering has holes; it is not a count of bonded pins. */
#define RK3506_GPIO_PIN_MASK(bank) \
	((bank) == 0 ? 0x01ffffffU : \
	 (bank) == 1 ? 0x0fffffffU : \
	 (bank) == 2 ? 0x0001ff3fU : \
	 (bank) == 3 ? 0x00007fffU : \
	 (bank) == 4 ? 0x00000f3fU : 0U)

/* SoC-private validation lets GPIO reject unsupported pads before changing DDR/DR. */
int rk3506_pinctrl_validate(const pinctrl_soc_pin_t *pin);

#define RK3506_DT_FLAGS(node_id) \
	((DT_PROP_OR(node_id, bias_disable, 0) * RK3506_PIN_BIAS_DISABLE) | \
	 (DT_PROP_OR(node_id, bias_pull_up, 0) * RK3506_PIN_PULL_UP) | \
	 (DT_PROP_OR(node_id, bias_pull_down, 0) * RK3506_PIN_PULL_DOWN) | \
	 (DT_PROP_OR(node_id, input_enable, 0) * RK3506_PIN_INPUT_ENABLE) | \
	 (DT_PROP_OR(node_id, input_disable, 0) * RK3506_PIN_INPUT_DISABLE) | \
	 (DT_PROP_OR(node_id, input_schmitt_enable, 0) * RK3506_PIN_SCHMITT_ENABLE) | \
	 (DT_PROP_OR(node_id, input_schmitt_disable, 0) * RK3506_PIN_SCHMITT_DISABLE) | \
	 (DT_PROP_OR(node_id, drive_open_drain, 0) * RK3506_PIN_OPEN_DRAIN) | \
	 (DT_PROP_OR(node_id, drive_push_pull, 0) * RK3506_PIN_PUSH_PULL) | \
	 (DT_NODE_HAS_PROP(node_id, rockchip_drive_strength_level) * RK3506_PIN_DRIVE_LEVEL) | \
	 (DT_NODE_HAS_PROP(node_id, slew_rate) * RK3506_PIN_SLEW_RATE))

#define RK3506_DT_PIN(node_id, prop, idx) \
	{ \
		.pinmux = DT_PROP_BY_IDX(node_id, prop, idx), \
		.flags = RK3506_DT_FLAGS(node_id), \
		.drive_level = DT_PROP_OR(node_id, rockchip_drive_strength_level, 0), \
		.slew_rate = DT_PROP_OR(node_id, slew_rate, 0), \
	}

#define Z_PINCTRL_STATE_PIN_INIT(node_id, prop, idx) \
	DT_FOREACH_PROP_ELEM_SEP(DT_PHANDLE_BY_IDX(node_id, prop, idx), pinmux, \
				 RK3506_DT_PIN, (,)),

#define Z_PINCTRL_STATE_PINS_INIT(node_id, prop) \
	{ DT_FOREACH_PROP_ELEM(node_id, prop, Z_PINCTRL_STATE_PIN_INIT) }

#endif /* ZEPHYR_SOC_ARM_ROCKCHIP_RK3506_PINCTRL_SOC_H_ */
