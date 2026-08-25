/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "rk3506_rpmsg_demo.h"
#include "rk3506_amp_layout.h"
#include "rk3506_rpmsg_platform.h"
#include "rpmsg_lite.h"
#include "rpmsg_ns.h"

#define RK3506_RPMSG_PING_PREFIX "ping:"
#define RK3506_RPMSG_PONG_PREFIX "pong:"
#define RK3506_RPMSG_REPLY_BUF_SIZE 512U

#define RK3506_RPMSG_THREAD_STACK 4096
#define RK3506_RPMSG_THREAD_PRIO 5

static struct rpmsg_lite_instance *rpmsg_dev;
static struct rpmsg_lite_endpoint *rpmsg_ept;
static uint32_t rpmsg_rx_count;
static uint32_t rpmsg_tx_count;
static uint32_t rpmsg_tx_fail_count;
static uint32_t rpmsg_ns_sent;

K_THREAD_STACK_DEFINE(rk3506_rpmsg_stack, RK3506_RPMSG_THREAD_STACK);
static struct k_thread rk3506_rpmsg_thread;

static int32_t rk3506_rpmsg_rx_cb(void *payload, uint32_t payload_len,
				  uint32_t src, void *priv)
{
	int32_t ret;
	char reply[RK3506_RPMSG_REPLY_BUF_SIZE];
	size_t suffix_len;
	size_t reply_len;

	ARG_UNUSED(priv);

	rpmsg_rx_count++;
#ifdef CONFIG_SOC_RK3506_RPMSG_DEBUG_LOG
	printk("[RK3506][RPMSG] rx%u src=0x%x len=%u: %.*s\n",
	       rpmsg_rx_count, src, payload_len,
	       (int)payload_len, (char *)payload);
#endif

	if ((payload_len <= strlen(RK3506_RPMSG_PING_PREFIX)) ||
	    memcmp(payload, RK3506_RPMSG_PING_PREFIX,
		   strlen(RK3506_RPMSG_PING_PREFIX)) != 0) {
#ifdef CONFIG_SOC_RK3506_RPMSG_DEBUG_LOG
		printk("[RK3506][RPMSG] ignore non-ping payload\n");
#endif
		return RL_RELEASE;
	}

	suffix_len = payload_len - strlen(RK3506_RPMSG_PING_PREFIX);
	reply_len = strlen(RK3506_RPMSG_PONG_PREFIX) + suffix_len;
	if (reply_len > sizeof(reply)) {
		rpmsg_tx_fail_count++;
		printk("[RK3506][RPMSG] pong payload too large: %u\n", payload_len);
		return RL_RELEASE;
	}

	memcpy(reply, RK3506_RPMSG_PONG_PREFIX,
	       strlen(RK3506_RPMSG_PONG_PREFIX));
	memcpy(reply + strlen(RK3506_RPMSG_PONG_PREFIX),
	       (char *)payload + strlen(RK3506_RPMSG_PING_PREFIX),
	       suffix_len);

	ret = rpmsg_lite_send(rpmsg_dev, rpmsg_ept, src, reply,
			      (uint32_t)reply_len, RL_DONT_BLOCK);
	if (ret == RL_SUCCESS) {
		rpmsg_tx_count++;
#ifdef CONFIG_SOC_RK3506_RPMSG_DEBUG_LOG
		printk("[RK3506][RPMSG] tx%u dst=0x%x len=%u: %.*s\n",
		       rpmsg_tx_count, src, (uint32_t)reply_len,
		       (int)reply_len, reply);
#endif
	} else {
		rpmsg_tx_fail_count++;
		printk("[RK3506][RPMSG] pong send failed: %d\n", ret);
	}

	return RL_RELEASE;
}

static void rk3506_rpmsg_worker(void *p1, void *p2, void *p3)
{
	int32_t ret;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

#ifdef CONFIG_SOC_RK3506_BOOT_BANNER
	printk("[RK3506][RPMSG] remote init: shmem=0x%08x link=0x%x\n",
	       RK3506_RPMSG_SHMEM_BASE, RK3506_RPMSG_LINK_ID);
#endif

	rpmsg_dev = rpmsg_lite_remote_init((void *)RK3506_RPMSG_SHMEM_BASE,
					   RK3506_RPMSG_LINK_ID,
					   RL_NO_FLAGS);
	if (rpmsg_dev == NULL) {
		printk("[RK3506][RPMSG] remote init failed\n");
		return;
	}

	rpmsg_ept = rpmsg_lite_create_ept(rpmsg_dev, RK3506_RPMSG_EPT_ADDR,
					  rk3506_rpmsg_rx_cb, NULL);
	if (rpmsg_ept == NULL) {
		printk("[RK3506][RPMSG] endpoint create failed\n");
		return;
	}

	while (rpmsg_lite_is_link_up(rpmsg_dev) == 0U) {
		rk3506_rpmsg_platform_poll();
		k_sleep(K_MSEC(1));
	}
#ifdef CONFIG_SOC_RK3506_BOOT_BANNER
	printk("[RK3506][RPMSG] link up\n");
#endif

	ret = rpmsg_ns_announce(rpmsg_dev, rpmsg_ept, RK3506_RPMSG_CH_NAME,
				RL_NS_CREATE);
	if (ret == RL_SUCCESS) {
		rpmsg_ns_sent = 1U;
#ifdef CONFIG_SOC_RK3506_BOOT_BANNER
		printk("[RK3506][RPMSG] ns announce: %s addr=0x%x\n",
		       RK3506_RPMSG_CH_NAME, RK3506_RPMSG_EPT_ADDR);
#endif
	} else {
		printk("[RK3506][RPMSG] ns announce failed: %d\n", ret);
	}

	rk3506_rpmsg_platform_force_irq_enable();

	for (;;) {
#ifdef CONFIG_SOC_RK3506_RPMSG_IRQ_ONLY_AFTER_NS
#ifdef CONFIG_SOC_RK3506_RPMSG_KEEP_IRQ_ENABLE
		rk3506_rpmsg_platform_force_irq_enable();
#endif
		k_sleep(K_SECONDS(1));
#else
		rk3506_rpmsg_platform_poll();
		k_sleep(K_MSEC(1));
#endif
	}
}

void rk3506_rpmsg_demo_start(void)
{
	k_thread_create(&rk3506_rpmsg_thread, rk3506_rpmsg_stack,
			K_THREAD_STACK_SIZEOF(rk3506_rpmsg_stack),
			rk3506_rpmsg_worker, NULL, NULL, NULL,
			RK3506_RPMSG_THREAD_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&rk3506_rpmsg_thread, "rk3506_rpmsg");
}

uint32_t rk3506_rpmsg_demo_rx_count(void)
{
	return rpmsg_rx_count;
}

uint32_t rk3506_rpmsg_demo_tx_count(void)
{
	return rpmsg_tx_count;
}

uint32_t rk3506_rpmsg_demo_tx_fail_count(void)
{
	return rpmsg_tx_fail_count;
}

uint32_t rk3506_rpmsg_demo_ns_sent(void)
{
	return rpmsg_ns_sent;
}
