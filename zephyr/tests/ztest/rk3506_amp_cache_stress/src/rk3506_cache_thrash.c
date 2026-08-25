/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "rk3506_cache_thrash.h"

#define RK3506_THRASH_WORDS 8192U
#define RK3506_THRASH_STACK 2048U
#define RK3506_THRASH_PRIO  8

static uint32_t thrash_a[RK3506_THRASH_WORDS] __aligned(64);
static uint32_t thrash_b[RK3506_THRASH_WORDS] __aligned(64);
static uint32_t thrash_counter;
static uint32_t thrash_signature;
static uint32_t thrash_error_count;

K_THREAD_STACK_DEFINE(rk3506_cache_thrash_stack, RK3506_THRASH_STACK);
static struct k_thread rk3506_cache_thrash_thread;

static uint32_t rk3506_thrash_mix(uint32_t x)
{
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return x;
}

static void rk3506_cache_thrash_worker(void *p1, void *p2, void *p3)
{
	uint32_t seed = 0x35060002U;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (uint32_t i = 0U; i < RK3506_THRASH_WORDS; i++) {
		seed = rk3506_thrash_mix(seed + i);
		thrash_a[i] = seed;
		thrash_b[i] = ~seed;
	}

	for (;;) {
		uint32_t sig = 0x6b350600U ^ thrash_counter;

		for (uint32_t i = 0U; i < RK3506_THRASH_WORDS; i++) {
			uint32_t v = rk3506_thrash_mix(thrash_a[i] + i + thrash_counter);

			thrash_a[i] = v;
			thrash_b[RK3506_THRASH_WORDS - 1U - i] ^= v + i;
			sig ^= v + (sig << 3) + (sig >> 2);
		}

		/*
		 * This is a load generator.  The Linux-side stress tool asserts
		 * that this counter keeps moving; do not add probabilistic local
		 * predicates here, otherwise an 8-hour run can fail spuriously.
		 */
		thrash_signature = sig;
		thrash_counter++;
		k_yield();
	}
}

void rk3506_cache_thrash_start(void)
{
	k_thread_create(&rk3506_cache_thrash_thread, rk3506_cache_thrash_stack,
			K_THREAD_STACK_SIZEOF(rk3506_cache_thrash_stack),
			rk3506_cache_thrash_worker, NULL, NULL, NULL,
			RK3506_THRASH_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&rk3506_cache_thrash_thread, "rk3506_thrash");
}

uint32_t rk3506_cache_thrash_counter(void)
{
	return thrash_counter;
}

uint32_t rk3506_cache_thrash_signature(void)
{
	return thrash_signature;
}

uint32_t rk3506_cache_thrash_error_count(void)
{
	return thrash_error_count;
}
