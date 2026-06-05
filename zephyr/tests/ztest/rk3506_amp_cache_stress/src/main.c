/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "rk3506_amp_probe.h"
#include "rk3506_cache_thrash.h"
#include "rk3506_rpmsg_demo.h"
#include "rk3506_rpmsg_platform.h"

int main(void)
{
	uint32_t count = 0U;

#ifdef CONFIG_SOC_RK3506_BOOT_BANNER
	printk("[RK3506][CLEAN-ZEPHYR] main reached\n");
#endif

	rk3506_cache_thrash_start();
	rk3506_rpmsg_demo_start();

	for (;;) {
		rk3506_amp_probe_update(count);
#ifdef CONFIG_SOC_RK3506_RPMSG_DEBUG_LOG
		if ((count % 5U) == 0U) {
			printk("[RK3506][CLEAN-ZEPHYR] heartbeat %u uptime=%lld ms "
			       "mbox_rx=%u mbox_tx=%u mbox_irq=%u rpmsg_rx=%u rpmsg_tx=%u fail=%u "
			       "gic_tgt=0x%x->0x%x group=%u en=%u pend=%u act=%u pri=0x%x\n",
			       count, k_uptime_get(),
			       rk3506_rpmsg_platform_rx_count(),
			       rk3506_rpmsg_platform_tx_count(),
			       rk3506_rpmsg_platform_irq_count(),
			       rk3506_rpmsg_demo_rx_count(),
			       rk3506_rpmsg_demo_tx_count(),
			       rk3506_rpmsg_demo_tx_fail_count(),
			       rk3506_rpmsg_platform_gic_target_before(),
			       rk3506_rpmsg_platform_gic_target_after(),
			       rk3506_rpmsg_platform_gic_group(),
			       rk3506_rpmsg_platform_gic_enable(),
			       rk3506_rpmsg_platform_gic_pending(),
			       rk3506_rpmsg_platform_gic_active(),
			       rk3506_rpmsg_platform_gic_priority());
		}
#endif
		count++;
		k_sleep(K_SECONDS(1));
	}

	return 0;
}
