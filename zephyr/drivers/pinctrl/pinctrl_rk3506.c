/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT rockchip_rk3506_pinctrl

#include <errno.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define IOC_DS       0x100U
#define IOC_PULL     0x200U
#define IOC_IE       0x300U
#define IOC_SMT      0x400U
#define IOC_SLEW     0x600U
#define IOC_OD       0x700U
#define IOC_D0       0x830U
#define IOC_SARADC   0x840U
#define RMIO_SEL     0x80U
#define RMIO_MUX     7U
#define RMIO_MAX     0x62U

#define BIAS_MASK    (RK3506_PIN_BIAS_DISABLE | RK3506_PIN_PULL_UP | RK3506_PIN_PULL_DOWN)
#define INPUT_MASK   (RK3506_PIN_INPUT_ENABLE | RK3506_PIN_INPUT_DISABLE)
#define SCHMITT_MASK (RK3506_PIN_SCHMITT_ENABLE | RK3506_PIN_SCHMITT_DISABLE)
#define DRIVE_MASK   (RK3506_PIN_OPEN_DRAIN | RK3506_PIN_PUSH_PULL)
#define SHARED_MASK  (BIAS_MASK | SCHMITT_MASK | RK3506_PIN_DRIVE_LEVEL | RK3506_PIN_SLEW_RATE)
#define FLAGS_MASK   (SHARED_MASK | INPUT_MASK | DRIVE_MASK)

#define DEV_CFG(dev) ((const struct rk3506_pinctrl_config *)(dev)->config)
#define DEV_DATA(dev) ((struct rk3506_pinctrl_data *)(dev)->data)

struct rk3506_pinctrl_clock {
	const struct device *dev;
	clock_control_subsys_t id;
};

struct rk3506_pinctrl_config {
	DEVICE_MMIO_NAMED_ROM(ioc0);
	DEVICE_MMIO_NAMED_ROM(ioc1);
	DEVICE_MMIO_NAMED_ROM(ioc234);
	DEVICE_MMIO_NAMED_ROM(rmio);
	struct rk3506_pinctrl_clock clocks[3];
};

struct rk3506_pinctrl_data {
	DEVICE_MMIO_NAMED_RAM(ioc0);
	DEVICE_MMIO_NAMED_RAM(ioc1);
	DEVICE_MMIO_NAMED_RAM(ioc234);
	DEVICE_MMIO_NAMED_RAM(rmio);
	struct k_spinlock lock;
};

/* Each bit is an implemented IOC mux value, not an RMIO function number. */
static const uint16_t muxes[4][28] = {
	{0x83, 0x83, 0x83, 0x83, 0x83, 0x83, 0x83, 0x87,
	 0x87, 0x87, 0x87, 0x83, 0x83, 0x83, 0x87, 0x87,
	 0x85, 0x85, 0x87, 0x87, 0x87, 0x83, 0x87, 0x87, 0x07},
	{0x0f, 0x11f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f,
	 0x2f, 0xef, 0xff, 0xff, 0x3f, 0x3f, 0x3f, 0x3f,
	 0x13f, 0x17f, 0x1ff, 0x1ef, 0x10f, 0x10f, 0x10f, 0x10f,
	 0x11f, 0x1df, 0x1cf, 0x1cf},
	{0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0, 0,
	 0x07, 0x07, 0x07, 0x07, 0x0f, 0x0f, 0x0f, 0x0f, 0x0b},
	{0x03, 0x03, 0x03, 0x07, 0x07, 0x07, 0x07, 0x07,
	 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07},
};

static void hiword_write(uintptr_t addr, uint32_t mask, uint32_t value)
{
	sys_write32((mask << 16) | (value & mask), addr);
}

static int rmio_index(uint32_t bank, uint32_t pin)
{
	if (bank == 0U && pin < 24U) {
		return pin;
	}
	if (bank == 1U) {
		if (pin >= 9U && pin <= 11U) {
			return pin + 15U;
		}
		if (pin >= 18U && pin <= 19U) {
			return pin + 9U;
		}
		if (pin >= 25U && pin <= 27U) {
			return pin + 4U;
		}
	}
	return -EINVAL;
}

int rk3506_pinctrl_validate(const pinctrl_soc_pin_t *pin)
{
	uint32_t bank = RK3506_PINCTRL_BANK(pin->pinmux);
	uint32_t number = RK3506_PINCTRL_PIN(pin->pinmux);
	uint32_t mux = RK3506_PINCTRL_MUX(pin->pinmux);
	uint32_t rmio = RK3506_PINCTRL_RMIO(pin->pinmux);
	const uint16_t groups[] = {BIAS_MASK, INPUT_MASK, SCHMITT_MASK, DRIVE_MASK};
	bool special = (bank == 0U && number == 24U) || bank == 4U;

	if (bank > 4U || number >= 32U ||
	    (RK3506_GPIO_PIN_MASK(bank) & BIT(number)) == 0U ||
	    (pin->flags & ~FLAGS_MASK) != 0U) {
		return -EINVAL;
	}
	for (size_t i = 0; i < ARRAY_SIZE(groups); i++) {
		uint32_t selected = pin->flags & groups[i];

		if ((selected & (selected - 1U)) != 0U) {
			return -EINVAL;
		}
	}
	/* GPO4_A0..A5 are PHY outputs, not regular IOC pads. Do not fake a mux. */
	if (bank == 4U && number < 6U) {
		return -ENOTSUP;
	}
	if (bank == 4U) {
		if (mux > 1U || rmio != 0U ||
		    (mux == 1U && (pin->flags & RK3506_PIN_INPUT_ENABLE) != 0U)) {
			return -EINVAL;
		}
	} else if (mux > 8U || (muxes[bank][number] & BIT(mux)) == 0U ||
		   rmio > RMIO_MAX || (mux != RMIO_MUX && rmio != 0U) ||
		   (mux == RMIO_MUX && rmio_index(bank, number) < 0)) {
		return -EINVAL;
	}
	if (special && (pin->flags & RK3506_PIN_OPEN_DRAIN) != 0U) {
		return -ENOTSUP;
	}
	if (((pin->flags & RK3506_PIN_DRIVE_LEVEL) != 0U &&
	     pin->drive_level > (special ? 3U : 5U)) ||
	    ((pin->flags & RK3506_PIN_SLEW_RATE) != 0U &&
	     pin->slew_rate > (special ? 1U : 3U))) {
		return -EINVAL;
	}
	return 0;
}

static uintptr_t ioc_base(const struct device *dev, uint32_t bank)
{
	if (bank == 0U) {
		return DEVICE_MMIO_NAMED_GET(dev, ioc0);
	}
	if (bank == 1U) {
		return DEVICE_MMIO_NAMED_GET(dev, ioc1);
	}
	return DEVICE_MMIO_NAMED_GET(dev, ioc234);
}

static void apply_special(uintptr_t ioc, const pinctrl_soc_pin_t *pin)
{
	bool saradc = RK3506_PINCTRL_BANK(pin->pinmux) == 4U;
	uint32_t number = RK3506_PINCTRL_PIN(pin->pinmux);
	uint32_t mux = RK3506_PINCTRL_MUX(pin->pinmux);
	uint32_t pull_shift = saradc ? 13U : 5U;
	uint32_t ie_bit = saradc ? number - 4U : 2U;
	uint32_t smt_shift = saradc ? 8U : 9U;
	uint32_t ds_shift = saradc ? 10U : 3U;
	uint32_t slew_bit = saradc ? 12U : 8U;
	uint32_t mask = saradc ? BIT(ie_bit) : GENMASK(1, 0);
	uint32_t value = saradc ? (mux == 0U ? BIT(ie_bit) : 0U) : mux;

	if ((pin->flags & BIAS_MASK) != 0U) {
		mask |= 3U << pull_shift;
		if ((pin->flags & (RK3506_PIN_PULL_UP | RK3506_PIN_PULL_DOWN)) != 0U) {
			value |= BIT(pull_shift);
		}
		if ((pin->flags & RK3506_PIN_PULL_UP) != 0U) {
			value |= BIT(pull_shift + 1U);
		}
	}
	if ((pin->flags & INPUT_MASK) != 0U) {
		mask |= BIT(ie_bit);
		value &= ~BIT(ie_bit);
		value |= (pin->flags & RK3506_PIN_INPUT_ENABLE) ? BIT(ie_bit) : 0U;
	}
	if ((pin->flags & SCHMITT_MASK) != 0U) {
		mask |= 3U << smt_shift;
		value |= (pin->flags & RK3506_PIN_SCHMITT_ENABLE) ? 3U << smt_shift : 0U;
	}
	if ((pin->flags & RK3506_PIN_DRIVE_LEVEL) != 0U) {
		mask |= 3U << ds_shift;
		value |= pin->drive_level << ds_shift;
	}
	if ((pin->flags & RK3506_PIN_SLEW_RATE) != 0U) {
		mask |= BIT(slew_bit);
		value |= pin->slew_rate << slew_bit;
	}
	hiword_write(ioc + (saradc ? IOC_SARADC : IOC_D0), mask, value);
}

static void apply_pin(const struct device *dev, const pinctrl_soc_pin_t *pin)
{
	uint32_t bank = RK3506_PINCTRL_BANK(pin->pinmux);
	uint32_t number = RK3506_PINCTRL_PIN(pin->pinmux);
	uint32_t mux = RK3506_PINCTRL_MUX(pin->pinmux);
	uintptr_t ioc = ioc_base(dev, bank);
	uint32_t group_offset = bank * 16U + (number / 8U) * 4U;
	uint32_t bit = number % 8U;
	uint32_t shift = (number % 4U) * 4U;

	if ((bank == 0U && number == 24U) || bank == 4U) {
		apply_special(ioc, pin);
		return;
	}
	if ((pin->flags & BIAS_MASK) != 0U) {
		uint32_t pull = (pin->flags & RK3506_PIN_PULL_UP) ? 1U :
			       (pin->flags & RK3506_PIN_PULL_DOWN) ? 2U : 0U;

		hiword_write(ioc + IOC_PULL + group_offset, 3U << (bit * 2U),
			     pull << (bit * 2U));
	}
	if ((pin->flags & INPUT_MASK) != 0U) {
		hiword_write(ioc + IOC_IE + group_offset, BIT(bit),
			     (pin->flags & RK3506_PIN_INPUT_ENABLE) ? BIT(bit) : 0U);
	}
	if ((pin->flags & SCHMITT_MASK) != 0U) {
		hiword_write(ioc + IOC_SMT + group_offset, BIT(bit),
			     (pin->flags & RK3506_PIN_SCHMITT_ENABLE) ? BIT(bit) : 0U);
	}
	if ((pin->flags & DRIVE_MASK) != 0U) {
		hiword_write(ioc + IOC_OD + group_offset, BIT(bit),
			     (pin->flags & RK3506_PIN_OPEN_DRAIN) ? BIT(bit) : 0U);
	}
	if ((pin->flags & RK3506_PIN_DRIVE_LEVEL) != 0U) {
		uint32_t ds_shift = (number % 2U) * 8U;

		hiword_write(ioc + IOC_DS + bank * 64U + (number / 2U) * 4U,
			     0x3fU << ds_shift, (BIT(pin->drive_level + 1U) - 1U) << ds_shift);
	}
	if ((pin->flags & RK3506_PIN_SLEW_RATE) != 0U) {
		hiword_write(ioc + IOC_SLEW + group_offset, 3U << (bit * 2U),
			     pin->slew_rate << (bit * 2U));
	}
	/* Connect a route only after programming its RMIO selector. */
	if (mux == RMIO_MUX) {
		hiword_write(DEVICE_MMIO_NAMED_GET(dev, rmio) + RMIO_SEL +
			     (uint32_t)rmio_index(bank, number) * 4U, 0x7fU,
			     RK3506_PINCTRL_RMIO(pin->pinmux));
	}
	hiword_write(ioc + bank * 32U + (number / 4U) * 4U, 0xfU << shift, mux << shift);
}

static int validate_state(const pinctrl_soc_pin_t *pins, uint8_t count)
{
	const pinctrl_soc_pin_t *shared = NULL;
	uint32_t saradc_pins = 0U;

	for (uint8_t i = 0; i < count; i++) {
		int ret = rk3506_pinctrl_validate(&pins[i]);

		if (ret != 0) {
			return ret;
		}
		for (uint8_t j = 0; j < i; j++) {
			if ((pins[i].pinmux >> 16) == (pins[j].pinmux >> 16)) {
				return -EINVAL;
			}
		}
		if (RK3506_PINCTRL_BANK(pins[i].pinmux) != 4U ||
		    (pins[i].flags & SHARED_MASK) == 0U) {
			continue;
		}
		if (shared != NULL &&
		    (((pins[i].flags ^ shared->flags) & SHARED_MASK) != 0U ||
		     ((pins[i].flags & RK3506_PIN_DRIVE_LEVEL) != 0U &&
		      pins[i].drive_level != shared->drive_level) ||
		     ((pins[i].flags & RK3506_PIN_SLEW_RATE) != 0U &&
		      pins[i].slew_rate != shared->slew_rate))) {
			return -EINVAL;
		}
		shared = &pins[i];
		saradc_pins |= BIT(RK3506_PINCTRL_PIN(pins[i].pinmux));
	}
	/* These electrical fields affect all four SARADC pads, not just one pin. */
	return shared != NULL && saradc_pins != GENMASK(11, 8) ? -ENOTSUP : 0;
}

int pinctrl_configure_pins(const pinctrl_soc_pin_t *pins, uint8_t pin_cnt, uintptr_t reg)
{
	const struct device *dev = DEVICE_DT_GET(DT_DRV_INST(0));
	struct rk3506_pinctrl_data *data = dev->data;
	int ret;

	ARG_UNUSED(reg);
	if (pin_cnt == 0U) {
		return 0;
	}
	if (pins == NULL) {
		return -EINVAL;
	}
	if (!device_is_ready(dev)) {
		return -ENODEV;
	}
	ret = validate_state(pins, pin_cnt);
	if (ret != 0) {
		return ret;
	}
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	for (uint8_t i = 0U; i < pin_cnt; i++) {
		apply_pin(dev, &pins[i]);
	}
	k_spin_unlock(&data->lock, key);
	return 0;
}

static int rk3506_pinctrl_init(const struct device *dev)
{
	const struct rk3506_pinctrl_config *config = dev->config;

	for (size_t i = 0; i < ARRAY_SIZE(config->clocks); i++) {
		if (!device_is_ready(config->clocks[i].dev)) {
			return -ENODEV;
		}
		int ret = clock_control_on(config->clocks[i].dev, config->clocks[i].id);

		if (ret != 0 && ret != -EALREADY) {
			return ret;
		}
	}
	DEVICE_MMIO_NAMED_MAP(dev, ioc0, K_MEM_CACHE_NONE);
	DEVICE_MMIO_NAMED_MAP(dev, ioc1, K_MEM_CACHE_NONE);
	DEVICE_MMIO_NAMED_MAP(dev, ioc234, K_MEM_CACHE_NONE);
	DEVICE_MMIO_NAMED_MAP(dev, rmio, K_MEM_CACHE_NONE);
	return 0;
}

#define CLOCK_ENTRY(idx) \
	{.dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_IDX(0, idx)), \
	 .id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL_BY_IDX(0, idx, id)}

static const struct rk3506_pinctrl_config rk3506_pinctrl_config = {
	DEVICE_MMIO_NAMED_ROM_INIT_BY_NAME(ioc0, DT_DRV_INST(0)),
	DEVICE_MMIO_NAMED_ROM_INIT_BY_NAME(ioc1, DT_DRV_INST(0)),
	DEVICE_MMIO_NAMED_ROM_INIT_BY_NAME(ioc234, DT_DRV_INST(0)),
	DEVICE_MMIO_NAMED_ROM_INIT_BY_NAME(rmio, DT_DRV_INST(0)),
	.clocks = {CLOCK_ENTRY(0), CLOCK_ENTRY(1), CLOCK_ENTRY(2)},
};
static struct rk3506_pinctrl_data rk3506_pinctrl_data;

DEVICE_DT_INST_DEFINE(0, rk3506_pinctrl_init, NULL, &rk3506_pinctrl_data,
		      &rk3506_pinctrl_config, PRE_KERNEL_1, CONFIG_PINCTRL_RK3506_INIT_PRIORITY,
		      NULL);
