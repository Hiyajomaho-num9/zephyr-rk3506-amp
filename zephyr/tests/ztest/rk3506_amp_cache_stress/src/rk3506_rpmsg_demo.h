/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RK3506_RPMSG_DEMO_H
#define RK3506_RPMSG_DEMO_H

#include <stdint.h>

void rk3506_rpmsg_demo_start(void);
uint32_t rk3506_rpmsg_demo_rx_count(void);
uint32_t rk3506_rpmsg_demo_tx_count(void);
uint32_t rk3506_rpmsg_demo_tx_fail_count(void);
uint32_t rk3506_rpmsg_demo_ns_sent(void);

#endif /* RK3506_RPMSG_DEMO_H */
