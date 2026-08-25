/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RK3506_CACHE_THRASH_H
#define RK3506_CACHE_THRASH_H

#include <stdint.h>

#ifdef CONFIG_SOC_RK3506_CACHE_THRASH
void rk3506_cache_thrash_start(void);
uint32_t rk3506_cache_thrash_counter(void);
uint32_t rk3506_cache_thrash_signature(void);
uint32_t rk3506_cache_thrash_error_count(void);
#else
static inline void rk3506_cache_thrash_start(void)
{
}

static inline uint32_t rk3506_cache_thrash_counter(void)
{
	return 0U;
}

static inline uint32_t rk3506_cache_thrash_signature(void)
{
	return 0U;
}

static inline uint32_t rk3506_cache_thrash_error_count(void)
{
	return 0U;
}
#endif

#endif /* RK3506_CACHE_THRASH_H */
