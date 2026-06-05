/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

BUILD_ASSERT(DT_HAS_CHOSEN(zephyr_canbus), "ok3506b_s12_amp_uart1 must choose CAN bus");
BUILD_ASSERT(DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_canbus), okay),
	     "chosen CAN bus must be enabled");
BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_canbus), rockchip_rk3506_canfd),
	     "chosen CAN bus must use the RK3506 CAN-FD compatible");
BUILD_ASSERT(DT_PROP(DT_CHOSEN(zephyr_canbus), clock_frequency) == 300000000U,
	     "validated RK3506 CAN clock must stay at 300 MHz");
BUILD_ASSERT(DT_PROP(DT_CHOSEN(zephyr_canbus), bitrate) == 500000U,
	     "validated board default bitrate must stay at 500 kbit/s");
BUILD_ASSERT(DT_PROP(DT_CHOSEN(zephyr_canbus), sample_point) == 870U,
	     "validated board sample point must stay at 870 per mille");
BUILD_ASSERT(DT_PROP(DT_CHOSEN(zephyr_canbus), rx_max_filters) > 0,
	     "standard RX software filters must be available");

#define RK3506_CAN_TEST_DEV DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus))
#define RK3506_CAN_TEST_CLOCK_HZ 300000000U

static int check_eq(const char *name, long actual, long expected)
{
	if (actual != expected) {
		printk("[RK3506][CAN-API] FAIL %s actual=%ld expected=%ld\n",
		       name, actual, expected);
		return -EINVAL;
	}

	return 0;
}

static int check_ok(const char *name, int ret)
{
	if (ret != 0) {
		printk("[RK3506][CAN-API] FAIL %s ret=%d\n", name, ret);
		return ret;
	}

	return 0;
}

static void rk3506_can_test_rx_cb(const struct device *dev, struct can_frame *frame,
				  void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(frame);
	ARG_UNUSED(user_data);
}

static int expect_stopped(const struct device *dev)
{
	struct can_bus_err_cnt err_cnt = { 0 };
	enum can_state state;
	int ret;

	ret = can_get_state(dev, &state, &err_cnt);
	if (ret != 0) {
		return check_ok("can_get_state", ret);
	}

	return check_eq("state stopped", state, CAN_STATE_STOPPED);
}

static int check_static_dt(const struct device *dev)
{
	uint32_t core_clock = 0U;
	int ret;

	ret = can_get_core_clock(dev, &core_clock);
	if (ret != 0) {
		return check_ok("can_get_core_clock", ret);
	}

	if (check_eq("core clock", core_clock, RK3506_CAN_TEST_CLOCK_HZ) != 0) {
		return -EINVAL;
	}
	if (check_eq("standard filters", can_get_max_filters(dev, false),
		     DT_PROP(DT_CHOSEN(zephyr_canbus), rx_max_filters)) != 0) {
		return -EINVAL;
	}

	return check_eq("extended filters", can_get_max_filters(dev, true), 0);
}

static int check_capability_and_mode(const struct device *dev)
{
	can_mode_t cap = 0U;
	int ret;

	ret = can_get_capabilities(dev, &cap);
	if (ret != 0) {
		return check_ok("can_get_capabilities", ret);
	}
	if (check_eq("capabilities", cap, CAN_MODE_LOOPBACK) != 0) {
		return -EINVAL;
	}

	if (check_ok("set normal mode", can_set_mode(dev, CAN_MODE_NORMAL)) != 0) {
		return -EINVAL;
	}
	if (check_eq("get normal mode", can_get_mode(dev), CAN_MODE_NORMAL) != 0) {
		return -EINVAL;
	}
	if (check_ok("set loopback mode", can_set_mode(dev, CAN_MODE_LOOPBACK)) != 0) {
		return -EINVAL;
	}
	if (check_eq("get loopback mode", can_get_mode(dev), CAN_MODE_LOOPBACK) != 0) {
		return -EINVAL;
	}
	if (check_eq("reject listen-only", can_set_mode(dev, CAN_MODE_LISTENONLY),
		     -ENOTSUP) != 0) {
		return -EINVAL;
	}
	if (check_eq("reject one-shot", can_set_mode(dev, CAN_MODE_ONE_SHOT), -ENOTSUP) != 0) {
		return -EINVAL;
	}
	if (check_eq("reject CAN-FD", can_set_mode(dev, CAN_MODE_FD), -ENOTSUP) != 0) {
		return -EINVAL;
	}
	if (check_eq("reject triple-sampling", can_set_mode(dev, CAN_MODE_3_SAMPLES),
		     -ENOTSUP) != 0) {
		return -EINVAL;
	}

	ret = can_set_mode(dev, CAN_MODE_NORMAL);
	if (ret != 0) {
		return check_ok("restore normal mode", ret);
	}

	return expect_stopped(dev);
}

static int check_timing(const struct device *dev)
{
	const struct can_timing board_500k = {
		.sjw = 1U,
		.prop_seg = 43U,
		.phase_seg1 = 43U,
		.phase_seg2 = 13U,
		.prescaler = 6U,
	};
	const struct can_timing odd_prescaler = {
		.sjw = 1U,
		.prop_seg = 43U,
		.phase_seg1 = 43U,
		.phase_seg2 = 13U,
		.prescaler = 5U,
	};
	const struct can_timing zero_tseg = {
		.sjw = 1U,
		.prop_seg = 0U,
		.phase_seg1 = 0U,
		.phase_seg2 = 13U,
		.prescaler = 6U,
	};
	struct can_timing calculated = { 0 };
	int ret;

	ret = can_calc_timing(dev, &calculated, 500000U, 870U);
	if (ret < 0) {
		return check_ok("can_calc_timing 500k", ret);
	}
	if (check_ok("set 500k timing", can_set_timing(dev, &board_500k)) != 0) {
		return -EINVAL;
	}
	if (check_eq("reject odd prescaler", can_set_timing(dev, &odd_prescaler),
		     -ENOTSUP) != 0) {
		return -EINVAL;
	}
	if (check_eq("reject zero tseg", can_set_timing(dev, &zero_tseg), -ENOTSUP) != 0) {
		return -EINVAL;
	}

	return expect_stopped(dev);
}

static int check_stopped_state_and_send_rejects(const struct device *dev)
{
	struct can_frame frame = {
		.flags = 0U,
		.id = 0x1b,
		.dlc = 8U,
		.data = { 0x01, 0x01, 0x03, 0x03, 0x01, 0x00, 0x00, 0x00 },
	};

	if (expect_stopped(dev) != 0) {
		return -EINVAL;
	}
	if (check_eq("stop while stopped", can_stop(dev), -EALREADY) != 0) {
		return -EINVAL;
	}
	if (check_eq("send while stopped", can_send(dev, &frame, K_NO_WAIT, NULL, NULL),
		     -ENETDOWN) != 0) {
		return -EINVAL;
	}

	frame.flags = CAN_FRAME_RTR;
	if (check_eq("reject RTR frame", can_send(dev, &frame, K_NO_WAIT, NULL, NULL),
		     -ENOTSUP) != 0) {
		return -EINVAL;
	}

	frame.flags = CAN_FRAME_IDE;
	frame.id = 0x1fffffffU;
	if (check_eq("reject IDE frame", can_send(dev, &frame, K_NO_WAIT, NULL, NULL),
		     -ENOTSUP) != 0) {
		return -EINVAL;
	}

	frame.flags = 0U;
	frame.id = CAN_STD_ID_MASK + 1U;
	if (check_eq("reject bad standard ID", can_send(dev, &frame, K_NO_WAIT, NULL, NULL),
		     -EINVAL) != 0) {
		return -EINVAL;
	}

	frame.id = 0x1b;
	frame.dlc = CAN_MAX_DLC + 1U;
	return check_eq("reject bad DLC", can_send(dev, &frame, K_NO_WAIT, NULL, NULL),
			-EINVAL);
}

static int check_rx_filter(const struct device *dev)
{
	struct can_filter filter = {
		.id = 0x1b,
		.mask = CAN_STD_ID_MASK,
	};
	struct can_filter masked_filter = {
		.id = 0x10,
		.mask = 0x7f0,
	};
	struct can_filter ext_filter = {
		.flags = CAN_FILTER_IDE,
		.id = 0x1b,
		.mask = CAN_EXT_ID_MASK,
	};
	struct can_filter bad_id_filter = {
		.id = CAN_STD_ID_MASK + 1U,
		.mask = CAN_STD_ID_MASK,
	};
	struct can_filter bad_mask_filter = {
		.id = 0x1b,
		.mask = CAN_STD_ID_MASK + 1U,
	};
	int filter_ids[CONFIG_CAN_RK3506_MAX_FILTERS];
	int max_filters;
	int filter_id;

	if (expect_stopped(dev) != 0) {
		return -EINVAL;
	}

	max_filters = can_get_max_filters(dev, false);
	if (check_eq("max standard filters", max_filters, CONFIG_CAN_RK3506_MAX_FILTERS) != 0) {
		return -EINVAL;
	}

	filter_id = can_add_rx_filter(dev, rk3506_can_test_rx_cb, NULL, &filter);
	if (filter_id < 0) {
		return check_ok("add exact standard filter", filter_id);
	}
	can_remove_rx_filter(dev, filter_id);

	filter_id = can_add_rx_filter(dev, rk3506_can_test_rx_cb, NULL, &masked_filter);
	if (filter_id < 0) {
		return check_ok("add masked standard filter", filter_id);
	}
	can_remove_rx_filter(dev, filter_id);

	if (check_eq("reject null callback", can_add_rx_filter(dev, NULL, NULL, &filter),
		     -EINVAL) != 0) {
		return -EINVAL;
	}
	if (check_eq("reject null filter",
		     can_add_rx_filter(dev, rk3506_can_test_rx_cb, NULL, NULL),
		     -EINVAL) != 0) {
		return -EINVAL;
	}
	if (check_eq("reject extended filter",
		     can_add_rx_filter(dev, rk3506_can_test_rx_cb, NULL, &ext_filter),
		     -ENOTSUP) != 0) {
		return -EINVAL;
	}
	if (check_eq("reject bad filter id",
		     can_add_rx_filter(dev, rk3506_can_test_rx_cb, NULL, &bad_id_filter),
		     -EINVAL) != 0) {
		return -EINVAL;
	}
	if (check_eq("reject bad filter mask",
		     can_add_rx_filter(dev, rk3506_can_test_rx_cb, NULL, &bad_mask_filter),
		     -EINVAL) != 0) {
		return -EINVAL;
	}

	for (int i = 0; i < max_filters; i++) {
		filter_ids[i] = can_add_rx_filter(dev, rk3506_can_test_rx_cb, NULL, &filter);
		if (filter_ids[i] < 0) {
			return check_ok("fill standard filter slot", filter_ids[i]);
		}
	}

	if (check_eq("standard filter ENOSPC",
		     can_add_rx_filter(dev, rk3506_can_test_rx_cb, NULL, &filter),
		     -ENOSPC) != 0) {
		return -EINVAL;
	}

	for (int i = 0; i < max_filters; i++) {
		can_remove_rx_filter(dev, filter_ids[i]);
	}

	can_remove_rx_filter(dev, -1);
	can_remove_rx_filter(dev, max_filters);

	return 0;
}

ZTEST(rk3506_can_api, test_board_validated_api)
{
	const struct device *dev = RK3506_CAN_TEST_DEV;

	zassert_true(device_is_ready(dev), "CAN device not ready");
	zassert_ok(check_static_dt(dev), "static DTS/API check failed");
	zassert_ok(check_capability_and_mode(dev), "capability/mode check failed");
	zassert_ok(check_timing(dev), "timing check failed");
	zassert_ok(check_stopped_state_and_send_rejects(dev), "stopped/send check failed");
	zassert_ok(check_rx_filter(dev), "RX filter check failed");
}

ZTEST_SUITE(rk3506_can_api, NULL, NULL, NULL, NULL, NULL);
