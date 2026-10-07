/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * One-shot key signal: HIGH 50 s, LOW 250 ms, HIGH 20 s, LOW 250 ms, then HIGH.
 * Timing starts when the application configures the GPIO output.
 */

#include <inttypes.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(gpio_power_cycle, LOG_LEVEL_INF);

#if !DT_NODE_EXISTS(DT_NODELABEL(power_cycle_gpio))
#error "power-cycle gpio node missing from devicetree"
#endif

static const struct gpio_dt_spec sig =
	GPIO_DT_SPEC_GET(DT_NODELABEL(power_cycle_gpio), power_gpios);

static int set_level(int level, int64_t start)
{
	int ret = gpio_pin_set_dt(&sig, level);

	if (ret) {
		LOG_ERR("gpio_pin_set_dt(%d) failed: %d", level, ret);
		return ret;
	}
	LOG_INF("t=%" PRId64 " ms GPIO0_A2 -> %s",
		k_uptime_get() - start, level ? "HIGH" : "LOW");
	return 0;
}

int main(void)
{
	int ret;
	int64_t start;
	int64_t phase_start;

	if (!gpio_is_ready_dt(&sig)) {
		LOG_ERR("GPIO device not ready");
		return -ENODEV;
	}

	/* Preload HIGH before enabling the output direction. */
	ret = gpio_pin_configure_dt(&sig, GPIO_OUTPUT_HIGH);
	if (ret) {
		LOG_ERR("configure failed: %d", ret);
		return ret;
	}
	start = k_uptime_get();
	LOG_INF("GPIO0_A2 one-shot: HIGH 50s -> LOW 250ms -> HIGH 20s -> "
		"LOW 250ms -> HIGH forever");
	LOG_INF("t=0 ms GPIO0_A2 -> HIGH (gpio0 pin %u)", sig.pin);

	k_sleep(K_TIMEOUT_ABS_MS(start + 50000));
	ret = set_level(0, start);
	if (ret != 0) {
		return ret;
	}
	phase_start = k_uptime_get();
	k_sleep(K_TIMEOUT_ABS_MS(phase_start + 250));
	ret = set_level(1, start);
	if (ret != 0) {
		return ret;
	}
	phase_start = k_uptime_get();
	k_sleep(K_TIMEOUT_ABS_MS(phase_start + 20000));
	ret = set_level(0, start);
	if (ret != 0) {
		return ret;
	}
	phase_start = k_uptime_get();
	k_sleep(K_TIMEOUT_ABS_MS(phase_start + 250));
	ret = set_level(1, start);
	if (ret != 0) {
		return ret;
	}
	LOG_INF("one-shot complete; GPIO0_A2 stays HIGH, no repeat");

	return 0;
}
