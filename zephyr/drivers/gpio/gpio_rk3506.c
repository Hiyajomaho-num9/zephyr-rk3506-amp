/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT rockchip_rk3506_gpio

#include <errno.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

/* Low/high pairs: 16 GPIO bits per word, upper halfword is write enable. */
#define GPIO_DR          0x000U
#define GPIO_DDR         0x008U
#define GPIO_INT_EN      0x010U
#define GPIO_INT_MASK    0x018U
#define GPIO_INT_TYPE    0x020U
#define GPIO_INT_POL     0x028U
#define GPIO_INT_BOTH    0x030U
#define GPIO_INT_STATUS  0x050U
#define GPIO_EOI         0x060U
#define GPIO_EXT_PORT    0x070U
#define GPIO_VIRTUAL_EN  0x108U
#define GPIO_OS_STRIDE   0x1000U

#define DEV_CFG(dev) ((const struct rk3506_gpio_config *)(dev)->config)
#define DEV_DATA(dev) ((struct rk3506_gpio_data *)(dev)->data)

struct rk3506_gpio_config {
	struct gpio_driver_config common;
	DEVICE_MMIO_NAMED_ROM(regs);
	const struct device *clock;
	clock_control_subsys_t clock_id;
	const struct pinctrl_dev_config *pcfg;
	void (*irq_config_func)(void);
	uint8_t bank;
	uint8_t irq_group;
};

struct rk3506_gpio_data {
	struct gpio_driver_data common;
	DEVICE_MMIO_NAMED_RAM(regs);
	sys_slist_t callbacks;
	struct k_spinlock lock;
	uint32_t inputs;
	uint32_t irq_enabled;
	uintptr_t port_base;
	bool virtual;
	bool irq_connected;
};

static const uint16_t group_offsets[] = {0x100, 0x110, 0x118, 0x120};

static void write_masked(uintptr_t base, uint32_t reg, uint32_t mask, uint32_t value)
{
	if ((mask & 0xffffU) != 0U) {
		sys_write32((mask << 16) | (value & mask & 0xffffU), base + reg);
	}
	if ((mask >> 16) != 0U) {
		sys_write32((mask & 0xffff0000U) | ((value & mask) >> 16), base + reg + 4U);
	}
}

static uint32_t read_pair(uintptr_t base, uint32_t reg)
{
	return (sys_read32(base + reg) & 0xffffU) |
	       ((sys_read32(base + reg + 4U) & 0xffffU) << 16);
}

static bool valid_pin(const struct device *dev, gpio_pin_t pin)
{
	return pin < 32U && (DEV_CFG(dev)->common.port_pin_mask & BIT(pin)) != 0U;
}

static int gpio_pad_config(const struct rk3506_gpio_config *config, gpio_pin_t pin,
			   gpio_flags_t flags, pinctrl_soc_pin_t *pad)
{
	const gpio_flags_t supported = GPIO_INPUT | GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW |
		GPIO_OUTPUT_INIT_HIGH | GPIO_ACTIVE_LOW | GPIO_PULL_UP | GPIO_PULL_DOWN |
		GPIO_SINGLE_ENDED | GPIO_LINE_OPEN_DRAIN;

	if ((flags & ~supported) != 0U) {
		return -ENOTSUP;
	}
	if (((flags & (GPIO_PULL_UP | GPIO_PULL_DOWN)) == (GPIO_PULL_UP | GPIO_PULL_DOWN)) ||
	    ((flags & GPIO_LINE_OPEN_DRAIN) != 0U && (flags & GPIO_SINGLE_ENDED) == 0U) ||
	    ((flags & (GPIO_OUTPUT_INIT_LOW | GPIO_OUTPUT_INIT_HIGH)) ==
	     (GPIO_OUTPUT_INIT_LOW | GPIO_OUTPUT_INIT_HIGH)) ||
	    ((flags & GPIO_OUTPUT) == 0U &&
	     (flags & (GPIO_SINGLE_ENDED | GPIO_OUTPUT_INIT_LOW | GPIO_OUTPUT_INIT_HIGH)) != 0U)) {
		return -EINVAL;
	}
	if ((flags & GPIO_SINGLE_ENDED) != 0U && (flags & GPIO_LINE_OPEN_DRAIN) == 0U) {
		return -ENOTSUP;
	}
	pad->pinmux = RK3506_PINMUX(config->bank, pin, 0, 0);
	pad->flags = (flags & GPIO_SINGLE_ENDED) ? RK3506_PIN_OPEN_DRAIN : RK3506_PIN_PUSH_PULL;
	pad->flags |= (flags & GPIO_INPUT) ? RK3506_PIN_INPUT_ENABLE : RK3506_PIN_INPUT_DISABLE;
	if (config->bank == 4U) {
		/* Bias is shared by all four SARADC pads; configure it as a pinctrl group. */
		if ((flags & (GPIO_PULL_UP | GPIO_PULL_DOWN)) != 0U) {
			return -ENOTSUP;
		}
		if ((flags & (GPIO_INPUT | GPIO_OUTPUT)) != 0U) {
			pad->flags &= ~RK3506_PIN_INPUT_DISABLE;
			pad->flags |= RK3506_PIN_INPUT_ENABLE;
		}
	} else {
		pad->flags |= (flags & GPIO_PULL_UP) ? RK3506_PIN_PULL_UP :
			      (flags & GPIO_PULL_DOWN) ? RK3506_PIN_PULL_DOWN :
							RK3506_PIN_BIAS_DISABLE;
	}
	return rk3506_pinctrl_validate(pad);
}

static int rk3506_gpio_configure(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags)
{
	const struct rk3506_gpio_config *config = dev->config;
	struct rk3506_gpio_data *data = dev->data;
	pinctrl_soc_pin_t pad = {0};
	uint32_t bit;
	int ret;

	if (!valid_pin(dev, pin)) {
		return -EINVAL;
	}
	ret = gpio_pad_config(config, pin, flags, &pad);
	if (ret != 0) {
		return ret;
	}
	bit = BIT(pin);
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	/* Disable this pin only; never reset or clear a bank shared with another OS. */
	write_masked(data->port_base, GPIO_INT_MASK, bit, bit);
	write_masked(data->port_base, GPIO_INT_EN, bit, 0U);
	write_masked(data->port_base, GPIO_EOI, bit, bit);
	data->irq_enabled &= ~bit;
	write_masked(data->port_base, GPIO_DDR, bit, 0U);
	if ((flags & (GPIO_OUTPUT_INIT_LOW | GPIO_OUTPUT_INIT_HIGH)) != 0U) {
		write_masked(data->port_base, GPIO_DR, bit,
			     (flags & GPIO_OUTPUT_INIT_HIGH) ? bit : 0U);
	}
	/* Preload the output latch before connecting the GPIO function and output driver. */
	ret = pinctrl_configure_pins(&pad, 1, 0);
	if (ret == 0) {
		data->inputs = (data->inputs & ~bit) | ((flags & GPIO_INPUT) ? bit : 0U);
		write_masked(data->port_base, GPIO_DDR, bit, (flags & GPIO_OUTPUT) ? bit : 0U);
	}
	k_spin_unlock(&data->lock, key);
	return ret;
}

static int rk3506_gpio_port_get_raw(const struct device *dev, gpio_port_value_t *value)
{
	*value = sys_read32(DEV_DATA(dev)->port_base + GPIO_EXT_PORT) &
		 DEV_CFG(dev)->common.port_pin_mask;
	return 0;
}

static int rk3506_gpio_port_set_masked_raw(const struct device *dev, gpio_port_pins_t mask,
					 gpio_port_value_t value)
{
	struct rk3506_gpio_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	write_masked(data->port_base, GPIO_DR, mask & DEV_CFG(dev)->common.port_pin_mask, value);
	k_spin_unlock(&data->lock, key);
	return 0;
}

static int rk3506_gpio_port_set_bits_raw(const struct device *dev, gpio_port_pins_t pins)
{
	return rk3506_gpio_port_set_masked_raw(dev, pins, pins);
}

static int rk3506_gpio_port_clear_bits_raw(const struct device *dev, gpio_port_pins_t pins)
{
	return rk3506_gpio_port_set_masked_raw(dev, pins, 0U);
}

static int rk3506_gpio_port_toggle_bits(const struct device *dev, gpio_port_pins_t pins)
{
	struct rk3506_gpio_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);
	uint32_t value = read_pair(data->port_base, GPIO_DR);

	write_masked(data->port_base, GPIO_DR, pins & DEV_CFG(dev)->common.port_pin_mask,
		     value ^ pins);
	k_spin_unlock(&data->lock, key);
	return 0;
}

static void route_irq(const struct device *dev, gpio_pin_t pin)
{
	const struct rk3506_gpio_config *config = dev->config;
	uintptr_t base = DEVICE_MMIO_NAMED_GET(dev, regs);

	/* Virtual ownership is established by the boot firmware, not reassigned here. */
	if (!DEV_DATA(dev)->virtual) {
		for (size_t i = 0; i < ARRAY_SIZE(group_offsets); i++) {
			write_masked(base, group_offsets[i], BIT(pin),
				     i == config->irq_group ? BIT(pin) : 0U);
		}
	}
}

static int rk3506_gpio_pin_interrupt_configure(const struct device *dev, gpio_pin_t pin,
					       enum gpio_int_mode mode, enum gpio_int_trig trig)
{
	const struct rk3506_gpio_config *config = dev->config;
	struct rk3506_gpio_data *data = dev->data;
	uint32_t bit;
	int ret = 0;

	if (!valid_pin(dev, pin)) {
		return -EINVAL;
	}
	if (config->bank == 4U && pin < 6U) {
		return -ENOTSUP;
	}
	if (mode != GPIO_INT_MODE_DISABLED && mode != GPIO_INT_MODE_EDGE &&
	    mode != GPIO_INT_MODE_LEVEL) {
		return -ENOTSUP;
	}
	if (mode != GPIO_INT_MODE_DISABLED &&
	    ((trig != GPIO_INT_TRIG_LOW && trig != GPIO_INT_TRIG_HIGH &&
	      trig != GPIO_INT_TRIG_BOTH) ||
	     (mode == GPIO_INT_MODE_LEVEL && trig == GPIO_INT_TRIG_BOTH))) {
		return -EINVAL;
	}
	bit = BIT(pin);
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	if (mode != GPIO_INT_MODE_DISABLED &&
	    ((data->inputs & bit) == 0U || (read_pair(data->port_base, GPIO_DDR) & bit) != 0U)) {
		ret = -EINVAL;
		goto out;
	}
	write_masked(data->port_base, GPIO_INT_MASK, bit, bit);
	write_masked(data->port_base, GPIO_INT_EN, bit, 0U);
	data->irq_enabled &= ~bit;
	if (mode == GPIO_INT_MODE_DISABLED) {
		write_masked(data->port_base, GPIO_EOI, bit, bit);
		goto out;
	}
	write_masked(data->port_base, GPIO_INT_TYPE, bit, mode == GPIO_INT_MODE_EDGE ? bit : 0U);
	write_masked(data->port_base, GPIO_INT_POL, bit, trig == GPIO_INT_TRIG_HIGH ? bit : 0U);
	write_masked(data->port_base, GPIO_INT_BOTH, bit, trig == GPIO_INT_TRIG_BOTH ? bit : 0U);
	route_irq(dev, pin);
	write_masked(data->port_base, GPIO_EOI, bit, bit);
	if (!data->irq_connected) {
		config->irq_config_func();
		data->irq_connected = true;
	}
	data->irq_enabled |= bit;
	write_masked(data->port_base, GPIO_INT_EN, bit, bit);
	write_masked(data->port_base, GPIO_INT_MASK, bit, 0U);
out:
	k_spin_unlock(&data->lock, key);
	return ret;
}

static int rk3506_gpio_manage_callback(const struct device *dev, struct gpio_callback *cb, bool set)
{
	return gpio_manage_callback(&DEV_DATA(dev)->callbacks, cb, set);
}

static uint32_t rk3506_gpio_get_pending_int(const struct device *dev)
{
	struct rk3506_gpio_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);
	uint32_t pending = sys_read32(data->port_base + GPIO_INT_STATUS) & data->irq_enabled;

	k_spin_unlock(&data->lock, key);
	return pending;
}

static void rk3506_gpio_isr(const struct device *dev)
{
	struct rk3506_gpio_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);
	uint32_t pending = sys_read32(data->port_base + GPIO_INT_STATUS) & data->irq_enabled;

	/* Acknowledge before callbacks so an edge arriving in a callback is not lost. */
	write_masked(data->port_base, GPIO_EOI, pending, pending);
	k_spin_unlock(&data->lock, key);
	if (pending != 0U) {
		gpio_fire_callbacks(&data->callbacks, dev, pending);
	}
}

#ifdef CONFIG_GPIO_GET_DIRECTION
static int rk3506_gpio_port_get_direction(const struct device *dev, gpio_port_pins_t map,
					 gpio_port_pins_t *inputs, gpio_port_pins_t *outputs)
{
	struct rk3506_gpio_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	map &= DEV_CFG(dev)->common.port_pin_mask;
	if (inputs != NULL) {
		*inputs = data->inputs & map;
	}
	if (outputs != NULL) {
		*outputs = read_pair(data->port_base, GPIO_DDR) & map;
	}
	k_spin_unlock(&data->lock, key);
	return 0;
}
#endif

static int rk3506_gpio_init(const struct device *dev)
{
	const struct rk3506_gpio_config *config = dev->config;
	struct rk3506_gpio_data *data = dev->data;
	int ret;

	if (!device_is_ready(config->clock)) {
		return -ENODEV;
	}
	ret = clock_control_on(config->clock, config->clock_id);
	if (ret != 0 && ret != -EALREADY) {
		return ret;
	}
	DEVICE_MMIO_NAMED_MAP(dev, regs, K_MEM_CACHE_NONE);
	data->port_base = DEVICE_MMIO_NAMED_GET(dev, regs);
	data->virtual = (sys_read32(data->port_base + GPIO_VIRTUAL_EN) & BIT(0)) != 0U;
	if (data->virtual) {
		uint32_t owned = read_pair(data->port_base, group_offsets[config->irq_group]);

		if ((owned & config->common.port_pin_mask) != config->common.port_pin_mask) {
			return -EBUSY;
		}
		data->port_base += config->irq_group * GPIO_OS_STRIDE;
	}
	sys_slist_init(&data->callbacks);
	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	return ret == -ENOENT ? 0 : ret;
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
	.get_pending_int = rk3506_gpio_get_pending_int,
#ifdef CONFIG_GPIO_GET_DIRECTION
	.port_get_direction = rk3506_gpio_port_get_direction,
#endif
};

#define GPIO_BANK(n) DT_INST_PROP(n, rockchip_bank_id)
#define GPIO_GROUP(n) DT_INST_PROP(n, rockchip_irq_group)
/* The six D-PHY pads remain unavailable until PHY GPIO mode is implemented. */
#define GPIO_IMPLEMENTED_MASK(n) \
	(RK3506_GPIO_PIN_MASK(GPIO_BANK(n)) & (GPIO_BANK(n) == 4 ? 0xffffffc0U : UINT32_MAX))

#define RK3506_GPIO_INIT(n) \
	PINCTRL_DT_INST_DEFINE(n); \
	BUILD_ASSERT(DT_INST_REG_SIZE(n) >= 4 * GPIO_OS_STRIDE); \
	static void rk3506_gpio_irq_config_##n(void) \
	{ \
		IRQ_CONNECT(DT_INST_IRQ_BY_IDX(n, GPIO_GROUP(n), irq), \
			    DT_INST_IRQ_BY_IDX(n, GPIO_GROUP(n), priority), \
			    rk3506_gpio_isr, DEVICE_DT_INST_GET(n), 0); \
		irq_enable(DT_INST_IRQ_BY_IDX(n, GPIO_GROUP(n), irq)); \
	} \
	static const struct rk3506_gpio_config rk3506_gpio_cfg_##n = { \
		.common = {.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_DT_INST(n) & \
					   GPIO_IMPLEMENTED_MASK(n)}, \
		DEVICE_MMIO_NAMED_ROM_INIT(regs, DT_DRV_INST(n)), \
		.clock = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)), \
		.clock_id = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(n, id), \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n), \
		.irq_config_func = rk3506_gpio_irq_config_##n, \
		.bank = GPIO_BANK(n), \
		.irq_group = GPIO_GROUP(n), \
	}; \
	static struct rk3506_gpio_data rk3506_gpio_data_##n; \
	DEVICE_DT_INST_DEFINE(n, rk3506_gpio_init, NULL, &rk3506_gpio_data_##n, \
			      &rk3506_gpio_cfg_##n, PRE_KERNEL_1, \
			      CONFIG_GPIO_RK3506_INIT_PRIORITY, &rk3506_gpio_api);

DT_INST_FOREACH_STATUS_OKAY(RK3506_GPIO_INIT)
