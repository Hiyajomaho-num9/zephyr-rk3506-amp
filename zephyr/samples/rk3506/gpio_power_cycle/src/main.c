/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * GPIO0_A2 power-cycle stress signal for OK3506B-S12 AMP.
 *
 * Timeline (measured from CPU2 firmware boot):
 *   boot      -> drive HIGH immediately (defined level, no floating window)
 *   +50 s     -> LOW  for  2 s   (power-cut request)
 *   +52 s     -> HIGH for 15 s   (boot window)
 *   +67 s     -> LOW  for  2 s   (second power-cut request)
 *   +69 s     -> HIGH forever    (steady state)
 *
 * The external power controller watches this pad and cold-cycles the board;
 * AMP warm restarts are not involved.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gpio_power_cycle, LOG_LEVEL_INF);

#if !DT_NODE_EXISTS(DT_NODELABEL(power_cycle_gpio))
#error "power-cycle gpio node missing from devicetree"
#endif

static const struct gpio_dt_spec sig =
	GPIO_DT_SPEC_GET(DT_NODELABEL(power_cycle_gpio), power_gpios);

static void set_level(int level, const char *why)
{
	int ret = gpio_pin_set_dt(&sig, level);

	if (ret) {
		LOG_ERR("gpio_pin_set_dt(%d) failed: %d", level, ret);
		return;
	}
	LOG_INF("t=%6ds GPIO0_A2 -> %s (%s)",
		(int32_t)(k_uptime_get() / 1000), level ? "HIGH" : "LOW", why);
}

int main(void)
{
	int ret;

	if (!gpio_is_ready_dt(&sig)) {
		LOG_ERR("GPIO device not ready");
		return 0;
	}

	/* Boot into a defined HIGH state right away. */
	ret = gpio_pin_configure_dt(&sig, GPIO_OUTPUT_HIGH);
	if (ret) {
		LOG_ERR("configure failed: %d", ret);
		return 0;
	}
	LOG_INF("GPIO0_A2 power-cycle test start (gpio0 pin %u)",
		sig.pin);
	set_level(1, "boot, defined level");

	LOG_INF("waiting 50 s ...");
	k_sleep(K_SECONDS(50));
	set_level(0, "power-cut #1 (2 s)");

	k_sleep(K_SECONDS(2));
	set_level(1, "boot window #1 (15 s)");

	k_sleep(K_SECONDS(15));
	set_level(0, "power-cut #2 (2 s)");

	k_sleep(K_SECONDS(2));
	set_level(1, "steady state (forever)");

	for (;;) {
		k_sleep(K_FOREVER);
	}
	return 0;
}
