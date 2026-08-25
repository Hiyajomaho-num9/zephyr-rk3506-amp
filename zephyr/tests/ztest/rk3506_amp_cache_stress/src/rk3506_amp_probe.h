/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RK3506_AMP_PROBE_H
#define RK3506_AMP_PROBE_H

#include <stdint.h>

#ifdef CONFIG_SOC_RK3506_AMP_PROBE
void rk3506_amp_probe_update(uint32_t counter);
#else
static inline void rk3506_amp_probe_update(uint32_t counter)
{
	(void)counter;
}
#endif

#endif /* RK3506_AMP_PROBE_H */
