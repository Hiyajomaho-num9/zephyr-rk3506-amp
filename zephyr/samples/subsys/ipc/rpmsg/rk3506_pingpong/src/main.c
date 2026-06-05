/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "rk3506_rpmsg_demo.h"

int main(void)
{
#ifdef CONFIG_SOC_RK3506_BOOT_BANNER
	printk("[RK3506][RPMSG] sample main reached\n");
#endif

	rk3506_rpmsg_demo_start();

	for (;;) {
		k_sleep(K_FOREVER);
	}

	return 0;
}
