/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT rockchip_rk3506_gpio

#include <errno.h>

#include <zephyr/arch/cpu.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

/*
 * RK3506 GPIO register layout (RK3506 TRM Part 1, section 17.4).
 *
 * Every control register is split into a low word (pins 0..15) and a high
 * word (pins 16..31) variant.  The upper 16 bits of each word form an
 * individual write-enable mask: bit (n + 16) gates writes to bit n.
 */
#define GPIO_SWPORT_DR_L        0x0000U
#define GPIO_SWPORT_DR_H        0x0004U
#define GPIO_SWPORT_DDR_L       0x0008U
#define GPIO_SWPORT_DDR_H       0x000cU
#define GPIO_INT_EN_L           0x0010U
#define GPIO_INT_EN_H           0x0014U
#define GPIO_INT_MASK_L         0x0018U
#define GPIO_INT_MASK_H         0x001cU
#define GPIO_INT_TYPE_L         0x0020U
#define GPIO_INT_TYPE_H         0x0024U
#define GPIO_INT_POLARITY_L     0x0028U
#define GPIO_INT_POLARITY_H     0x002cU
#define GPIO_INT_BOTHEDGE_L     0x0030U
#define GPIO_INT_BOTHEDGE_H     0x0034U
#define GPIO_INT_STATUS         0x0050U
#define GPIO_PORT_EOI_L         0x0060U
#define GPIO_PORT_EOI_H         0x0064U
#define GPIO_EXT_PORT           0x0070U

#define RK3506_GPIO_BANK_PINS   16U /* pins handled per low/high register pair */

/* Build a value with per-bit write-enable bits in the upper half word. */
#define RK3506_GPIO_WR(regval)  ((((regval) & 0xffffU) << 16) | ((regval) & 0xffffU))

struct rk3506_gpio_clock {
	const struct device *dev;
	clock_control_subsys_t id;
};

struct rk3506_gpio_config {
	/* struct gpio_driver_config must come first. */
	struct gpio_driver_config common;
	uintptr_t base;
	uint8_t ngpios;
	void (*irq_config_func)(const struct device *port);
	const struct rk3506_gpio_clock *clock;
};

struct rk3506_gpio_data {
	struct gpio_driver_data common;
	sys_slist_t callbacks;
	/* cache of the software port data register for read-modify-write */
	uint32_t dr_cache;
};

/*
 * Update the low/high register pair that contains @pin: clear @mask in the
 * current half word value, then OR in @value, using a read-modify-write on
 * the affected half only.  The upper half of the written word re-arms the
 * per-bit write enable.
 */
static void rk3506_gpio_update_pair(uintptr_t base, uintptr_t low_ofs,
				    uintptr_t high_ofs, gpio_pin_t pin,
				    uint32_t mask, uint32_t value)
{
	uintptr_t addr = base + ((pin >= RK3506_GPIO_BANK_PINS) ? high_ofs : low_ofs);
	uint32_t half = (pin >= RK3506_GPIO_BANK_PINS) ? (pin - RK3506_GPIO_BANK_PINS) : pin;
	uint32_t regval;

	regval = sys_read32(addr);
	regval &= ~((mask & 0xffffU) << half);
	regval |= (value & mask & 0xffffU) << half;
	sys_write32(RK3506_GPIO_WR(regval), addr);
}

static void rk3506_gpio_write_dr(const struct rk3506_gpio_config *config,
				 uint32_t value)
{
	sys_write32(RK3506_GPIO_WR(value & 0xffffU),
		    config->base + GPIO_SWPORT_DR_L);
	sys_write32(RK3506_GPIO_WR((value >> 16) & 0xffffU),
		    config->base + GPIO_SWPORT_DR_H);
}

static int rk3506_gpio_configure(const struct device *port, gpio_pin_t pin,
				 gpio_flags_t flags)
{
	const struct rk3506_gpio_config *config = port->config;

	if (pin >= config->ngpios) {
		return -EINVAL;
	}

	if (((flags & GPIO_OUTPUT) != 0U) && ((flags & GPIO_INPUT) != 0U)) {
		return -ENOTSUP;
	}

	if ((flags & GPIO_OUTPUT_INIT_LOW) != 0U) {
		rk3506_gpio_update_pair(config->base, GPIO_SWPORT_DR_L,
					GPIO_SWPORT_DR_H, pin, 0xffffU, 0U);
	} else if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0U) {
		rk3506_gpio_update_pair(config->base, GPIO_SWPORT_DR_L,
					GPIO_SWPORT_DR_H, pin, 0xffffU, 0xffffU);
	}

	if ((flags & GPIO_OUTPUT) != 0U) {
		rk3506_gpio_update_pair(config->base, GPIO_SWPORT_DDR_L,
					GPIO_SWPORT_DDR_H, pin, 0xffffU, 0xffffU);
	} else if ((flags & GPIO_INPUT) != 0U) {
		rk3506_gpio_update_pair(config->base, GPIO_SWPORT_DDR_L,
					GPIO_SWPORT_DDR_H, pin, 0xffffU, 0U);
	}

	return 0;
}

static int rk3506_gpio_port_get_raw(const struct device *port,
				    gpio_port_value_t *value)
{
	const struct rk3506_gpio_config *config = port->config;

	*value = (gpio_port_value_t)sys_read32(config->base + GPIO_EXT_PORT);

	return 0;
}

static int rk3506_gpio_port_set_masked_raw(const struct device *port,
					   gpio_port_pins_t mask,
					   gpio_port_value_t value)
{
	const struct rk3506_gpio_config *config = port->config;
	struct rk3506_gpio_data *data = port->data;
	uint32_t regval;

	regval = (data->dr_cache & ~mask) | (value & mask);
	data->dr_cache = regval;
	rk3506_gpio_write_dr(config, regval);

	return 0;
}

static int rk3506_gpio_port_set_bits_raw(const struct device *port,
					 gpio_port_pins_t pins)
{
	const struct rk3506_gpio_config *config = port->config;
	struct rk3506_gpio_data *data = port->data;

	data->dr_cache |= pins;
	rk3506_gpio_write_dr(config, data->dr_cache);

	return 0;
}

static int rk3506_gpio_port_clear_bits_raw(const struct device *port,
					   gpio_port_pins_t pins)
{
	const struct rk3506_gpio_config *config = port->config;
	struct rk3506_gpio_data *data = port->data;

	data->dr_cache &= ~pins;
	rk3506_gpio_write_dr(config, data->dr_cache);

	return 0;
}

static int rk3506_gpio_port_toggle_bits(const struct device *port,
					gpio_port_pins_t pins)
{
	const struct rk3506_gpio_config *config = port->config;
	struct rk3506_gpio_data *data = port->data;

	data->dr_cache ^= pins;
	rk3506_gpio_write_dr(config, data->dr_cache);

	return 0;
}

static int rk3506_gpio_pin_interrupt_configure(const struct device *port,
					       gpio_pin_t pin,
					       enum gpio_int_mode mode,
					       enum gpio_int_trig trig)
{
	const struct rk3506_gpio_config *config = port->config;
	uintptr_t base = config->base;
	uint32_t pin_bit = BIT(pin % RK3506_GPIO_BANK_PINS);
	uint32_t eoi_ofs = (pin >= RK3506_GPIO_BANK_PINS) ? GPIO_PORT_EOI_H
							  : GPIO_PORT_EOI_L;
	uint32_t int_type;
	uint32_t polarity;
	uint32_t bothedge;

	if (pin >= config->ngpios) {
		return -EINVAL;
	}

	if (mode == GPIO_INT_MODE_DISABLED) {
		/* Disable, mask and clear any stale pending state. */
		rk3506_gpio_update_pair(base, GPIO_INT_EN_L, GPIO_INT_EN_H,
					pin, 0xffffU, 0U);
		rk3506_gpio_update_pair(base, GPIO_INT_MASK_L, GPIO_INT_MASK_H,
					pin, 0xffffU, 0xffffU);
		sys_write32(pin_bit, base + eoi_ofs);
		return 0;
	}

	/*
	 * INT_TYPE: 0 = level sensitive, 1 = edge sensitive.
	 * INT_POLARITY: 0 = active-low / falling, 1 = active-high / rising.
	 * INT_BOTHEDGE: separate register, 1 = trigger on both edges.
	 */
	int_type = (mode == GPIO_INT_MODE_EDGE) ? 0xffffU : 0U;
	if (trig == GPIO_INT_TRIG_BOTH) {
		polarity = 0U;
		bothedge = 0xffffU;
	} else {
		polarity = ((trig & GPIO_INT_TRIG_HIGH) != 0U) ? 0xffffU : 0U;
		bothedge = 0U;
	}

	rk3506_gpio_update_pair(base, GPIO_INT_TYPE_L, GPIO_INT_TYPE_H,
				pin, 0xffffU, int_type);
	rk3506_gpio_update_pair(base, GPIO_INT_POLARITY_L, GPIO_INT_POLARITY_H,
				pin, 0xffffU, polarity);
	rk3506_gpio_update_pair(base, GPIO_INT_BOTHEDGE_L, GPIO_INT_BOTHEDGE_H,
				pin, 0xffffU, bothedge);

	/* Clear stale pending, then unmask and enable the interrupt. */
	sys_write32(pin_bit, base + eoi_ofs);
	rk3506_gpio_update_pair(base, GPIO_INT_MASK_L, GPIO_INT_MASK_H,
				pin, 0xffffU, 0U);
	rk3506_gpio_update_pair(base, GPIO_INT_EN_L, GPIO_INT_EN_H,
				pin, 0xffffU, 0xffffU);

	return 0;
}

static int rk3506_gpio_manage_callback(const struct device *port,
				       struct gpio_callback *cb, bool set)
{
	struct rk3506_gpio_data *data = port->data;

	return gpio_manage_callback(&data->callbacks, cb, set);
}

static void rk3506_gpio_isr(const struct device *port)
{
	const struct rk3506_gpio_config *config = port->config;
	struct rk3506_gpio_data *data = port->data;
	uint32_t int_status;

	int_status = sys_read32(config->base + GPIO_INT_STATUS);
	if (int_status == 0U) {
		return;
	}

	gpio_fire_callbacks(&data->callbacks, port, int_status);

	/* Edge interrupts are cleared via EOI (level is cleared at source). */
	sys_write32(int_status & 0xffffU, config->base + GPIO_PORT_EOI_L);
	sys_write32((int_status >> 16) & 0xffffU, config->base + GPIO_PORT_EOI_H);
}

static int rk3506_gpio_init(const struct device *port)
{
	const struct rk3506_gpio_config *config = port->config;
	struct rk3506_gpio_data *data = port->data;
	int ret;

	if ((config->clock != NULL) && (config->clock->dev != NULL)) {
		ret = clock_control_on(config->clock->dev, config->clock->id);
		if ((ret != 0) && (ret != -EALREADY)) {
			return ret;
		}
	}

	data->dr_cache = (uint32_t)(sys_read32(config->base + GPIO_SWPORT_DR_L) & 0xffffU) |
			 ((uint32_t)(sys_read32(config->base + GPIO_SWPORT_DR_H) & 0xffffU) << 16);
	sys_slist_init(&data->callbacks);

	config->irq_config_func(port);

	return 0;
}

static DEVICE_API(gpio, rk3506_gpio_api) = {
	.pin_configure = rk3506_gpio_configure,
	.port_get_raw = rk3506_gpio_port_get_raw,
	.port_set_masked_raw = rk3506_gpio_port_set_masked_raw,
	.port_set_bits_raw = rk3506_gpio_port_set_bits_raw,
	.port_clear_bits_raw = rk3506_gpio_port_clear_bits_raw,
	.port_toggle_bits = rk3506_gpio_port_toggle_bits,
	.pin_interrupt_configure = rk3506_gpio_pin_interrupt_configure,
	.manage_callback = rk3506_gpio_manage_callback,
};

/*
 * Clock handle: two variants selected at DT time.  When the node has a
 * clocks property the clock struct references the CRU device; otherwise a
 * NULL pointer keeps the config initializer constant.
 */
#define RK3506_GPIO_CLOCK_DEFINE(n)                                                \
	static const struct rk3506_gpio_clock rk3506_gpio_clock_on_##n = {         \
		.dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),                      \
		.id = (clock_control_subsys_t)(uintptr_t)DT_INST_PHA(n, clocks,    \
								    id),           \
	};                                                                         \
	static const struct rk3506_gpio_clock *const rk3506_gpio_clock_ptr_##n =   \
		COND_CODE_1(DT_NODE_HAS_STATUS(DT_DRV_INST(n), okay),              \
			    (&rk3506_gpio_clock_on_##n), (NULL));

#define RK3506_GPIO_CLOCK_PTR(n) rk3506_gpio_clock_ptr_##n

#define RK3506_GPIO_INIT(n)                                                        \
	static void rk3506_gpio_irq_config_##n(const struct device *port)          \
	{                                                                          \
		IRQ_CONNECT(DT_INST_IRQ_BY_IDX(n, 0, irq),                         \
			    DT_INST_IRQ_BY_IDX(n, 0, priority), rk3506_gpio_isr,   \
			    DEVICE_DT_INST_GET(n), 0);                             \
		irq_enable(DT_INST_IRQ_BY_IDX(n, 0, irq));                         \
	}                                                                          \
	RK3506_GPIO_CLOCK_DEFINE(n)                                                \
	static const struct rk3506_gpio_config rk3506_gpio_cfg_##n = {             \
		.common =                                                          \
			{                                                          \
				.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_DT_INST(n),\
			},                                                         \
		.base = DT_INST_REG_ADDR(n),                                       \
		.irq_config_func = rk3506_gpio_irq_config_##n,                     \
		.clock = RK3506_GPIO_CLOCK_PTR(n),                                 \
	};                                                                         \
	static struct rk3506_gpio_data rk3506_gpio_data_##n;                       \
	DEVICE_DT_INST_DEFINE(n, rk3506_gpio_init, NULL, &rk3506_gpio_data_##n,    \
			      &rk3506_gpio_cfg_##n, PRE_KERNEL_1,                  \
			      CONFIG_GPIO_INIT_PRIORITY, &rk3506_gpio_api);

DT_INST_FOREACH_STATUS_OKAY(RK3506_GPIO_INIT)
