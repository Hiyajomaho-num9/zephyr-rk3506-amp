/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_RK3506_PINCTRL_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_RK3506_PINCTRL_H_

#define RK3506_PINCTRL_BANK_A 0
#define RK3506_PINCTRL_BANK_B 1
#define RK3506_PINCTRL_BANK_C 2
#define RK3506_PINCTRL_BANK_D 3

/* Bank 0..4; pin is the bank-local number: A0=0, B0=8, C0=16, D0=24. */
#define RK3506_PINMUX(bank, pin, mux, rmio_func) \
	(((bank) << 24) | ((pin) << 16) | ((mux) << 8) | (rmio_func))

/* Compatibility spelling for existing GPIO0 group-based board definitions. */
#define RK3506_PINCTRL(group, pin, mux, rmio_func) \
	RK3506_PINMUX(0, (group) * 8 + (pin), mux, rmio_func)

#define RK3506_PINCTRL_BANK(value) (((value) >> 24) & 0xff)
#define RK3506_PINCTRL_PIN(value) (((value) >> 16) & 0xff)
#define RK3506_PINCTRL_MUX(value) (((value) >> 8) & 0xff)
#define RK3506_PINCTRL_RMIO(value) ((value) & 0xff)

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_RK3506_PINCTRL_H_ */
