/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_RK3506_CRU_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_RK3506_CRU_H_

#define HCLK_CAN0 145
#define CLK_CAN0  146
#define HCLK_CAN1 147
#define CLK_CAN1  148

/* GPIO bus clock gates (pclk domain). */
#define HCLK_GPIO0 150
#define HCLK_GPIO1 151
#define HCLK_GPIO2 152
#define HCLK_GPIO3 153
#define HCLK_GPIO4 154

#define PCLK_GPIO0_IOC 155
#define PCLK_GPIO1_IOC 156
#define PCLK_GPIO234_IOC 157

#define CLK_SPI0 158
#define PCLK_SPI0 159
#define CLK_SPI1 160
#define PCLK_SPI1 161

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_RK3506_CRU_H_ */
