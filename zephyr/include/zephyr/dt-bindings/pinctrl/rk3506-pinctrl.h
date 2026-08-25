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

#define RK3506_PINCTRL(bank, pin, mux, rmio_func) \
	(bank) (pin) (mux) (rmio_func)

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_RK3506_PINCTRL_H_ */
