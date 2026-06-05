/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT rockchip_rk3506_canfd

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(can_rk3506, CONFIG_CAN_LOG_LEVEL);

#define RK3506_CAN_MODE               0x0000U
#define RK3506_CAN_CMD                0x0004U
#define RK3506_CAN_STATE              0x0008U
#define RK3506_CAN_INT                0x000cU
#define RK3506_CAN_INT_MASK           0x0010U
#define RK3506_CAN_NBTP               0x0100U
#define RK3506_CAN_DBTP               0x0104U
#define RK3506_CAN_TDCR               0x0108U
#define RK3506_CAN_BRS_CFG            0x010cU
#define RK3506_CAN_DMA_CTRL           0x011cU
#define RK3506_CAN_TXFIC              0x0200U
#define RK3506_CAN_TXID               0x0204U
#define RK3506_CAN_TXDAT0             0x0208U
#define RK3506_CAN_RXFRD              0x0400U
#define RK3506_CAN_STR_CTL            0x0600U
#define RK3506_CAN_STR_STATE          0x0604U
#define RK3506_CAN_STR_WTM            0x060cU
#define RK3506_CAN_ATF0               0x0700U
#define RK3506_CAN_ATFM0              0x0714U
#define RK3506_CAN_ATF_DLC            0x0728U
#define RK3506_CAN_ATF_CTL            0x072cU
#define RK3506_CAN_AUTO_RETX_CFG      0x0808U
#define RK3506_CAN_RXINT_CTRL         0x0818U
#define RK3506_CAN_RXINT_TIMEOUT      0x081cU
#define RK3506_CAN_BUSOFFRCY_CFG      0x0830U
#define RK3506_CAN_BUSOFF_RCY_THR     0x0834U
#define RK3506_CAN_ERROR_CODE         0x0900U
#define RK3506_CAN_ERROR_MASK         0x0904U
#define RK3506_CAN_RXERRORCNT         0x0910U
#define RK3506_CAN_TXERRORCNT         0x0914U

#define RK3506_CAN_MODE_WORK          BIT(0)
#define RK3506_CAN_MODE_LBACK         BIT(4)

#define RK3506_CAN_CMD_TX0_REQ        BIT(0)
#define RK3506_CAN_CMD_TX1_REQ        BIT(1)

#define RK3506_CAN_INT_RX_FINISH      BIT(0)
#define RK3506_CAN_INT_TX_FINISH      BIT(1)
#define RK3506_CAN_INT_ERR_WARN       BIT(2)
#define RK3506_CAN_INT_RX_BUF_OV      BIT(3)
#define RK3506_CAN_INT_PASSIVE_ERR    BIT(4)
#define RK3506_CAN_INT_BUS_ERR        BIT(6)
#define RK3506_CAN_INT_RX_STR_FULL    BIT(7)
#define RK3506_CAN_INT_RX_STR_OV      BIT(8)
#define RK3506_CAN_INT_BUS_OFF        BIT(9)
#define RK3506_CAN_INT_BUS_OFF_RECOVERY BIT(10)
#define RK3506_CAN_INT_AUTO_RETX_FAIL BIT(12)
#define RK3506_CAN_INT_RX_STR_TIMEOUT BIT(15)
#define RK3506_CAN_INT_ISM_WTM        BIT(17)
#define RK3506_CAN_INT_ALL_WRITABLE   0x000fffffU
#define RK3506_CAN_INT_RX_MASK        (RK3506_CAN_INT_RX_FINISH | \
					RK3506_CAN_INT_RX_STR_TIMEOUT | \
					RK3506_CAN_INT_ISM_WTM | \
					RK3506_CAN_INT_RX_STR_FULL)
#define RK3506_CAN_INT_ERROR_MASK     (RK3506_CAN_INT_ERR_WARN | \
					RK3506_CAN_INT_RX_BUF_OV | \
					RK3506_CAN_INT_PASSIVE_ERR | \
					RK3506_CAN_INT_BUS_ERR | \
					RK3506_CAN_INT_BUS_OFF | \
					RK3506_CAN_INT_BUS_OFF_RECOVERY | \
					RK3506_CAN_INT_AUTO_RETX_FAIL | \
					RK3506_CAN_INT_RX_STR_OV)

#define RK3506_CAN_STATE_ERR_WARN     BIT(3)
#define RK3506_CAN_STATE_BUS_OFF      BIT(4)

#define RK3506_CAN_NBTP_NSJW_SHIFT    24U
#define RK3506_CAN_NBTP_NBRP_SHIFT    16U
#define RK3506_CAN_NBTP_NTSEG2_SHIFT  8U
#define RK3506_CAN_NBTP_NTSEG1_SHIFT  0U

#define RK3506_CAN_TX_DLC_MASK        GENMASK(3, 0)

#define RK3506_CAN_RX_FORMAT_EXT      BIT(23)
#define RK3506_CAN_RX_RTR             BIT(22)
#define RK3506_CAN_RX_FDF             BIT(21)
#define RK3506_CAN_RX_BRS             BIT(20)
#define RK3506_CAN_RX_DLC_SHIFT       24U
#define RK3506_CAN_RX_DLC_MASK        GENMASK(27, 24)

#define RK3506_CAN_STR_INTM_CNT_SHIFT 17U
#define RK3506_CAN_STR_INTM_CNT_MASK  GENMASK(25, 17)
#define RK3506_CAN_STR_INTM_LEFT_SHIFT 8U
#define RK3506_CAN_STR_INTM_LEFT_MASK GENMASK(16, 8)
#define RK3506_CAN_STR_INTM_EMPTY     BIT(0)

#define RK3506_CAN_STR_CTL_RX_RESET   BIT(4)
#define RK3506_CAN_STR_CTL_TIMEOUT    BIT(8)
#define RK3506_CAN_STR_CTL_ISM_CANFD_FIXED (2U << 2)
#define RK3506_CAN_STR_WTM_CANFD      0x0000006cU
#define RK3506_CAN_RX_WORDS           18U

#define RK3506_CAN_RETX_TIME_LIMIT_CNT 0x12cU
#define RK3506_CAN_RETX_TIME_LIMIT_SHIFT 3U
#define RK3506_CAN_RETX_LIMIT_EN      BIT(1)
#define RK3506_CAN_AUTO_RETX_EN       BIT(0)

#define RK3506_CAN_BUSOFF_RCY_MODE_EN BIT(8)
#define RK3506_CAN_BUSOFF_RCY_CNT_FAST 4U
#define RK3506_CAN_BUSOFF_RCY_TIME_FAST 0x003d0900U

#define RK3506_CAN_ERROR_MASK_ACK     BIT(4)

#define RK3506_CAN_SYNC_SEG           1U
#define RK3506_CAN_TIMING_SJW_MAX     128U
#define RK3506_CAN_TIMING_TSEG1_MAX   128U
#define RK3506_CAN_TIMING_TSEG2_MAX   128U
#define RK3506_CAN_TIMING_PRESC_MIN   2U
#define RK3506_CAN_TIMING_PRESC_MAX   256U

struct rk3506_can_clock {
	const struct device *dev;
	clock_control_subsys_t id;
	uint32_t rate;
};

struct rk3506_can_config {
	struct can_driver_config common;
	const struct pinctrl_dev_config *pincfg;
	const struct rk3506_can_clock *clocks;
	size_t clock_count;
	const struct reset_dt_spec *resets;
	size_t reset_count;
	uintptr_t base;
	uint32_t clock_frequency;
	void (*irq_config_func)(const struct device *dev);
	uint8_t instance;
};

struct rk3506_can_rx_filter {
	can_rx_callback_t callback;
	void *user_data;
	struct can_filter filter;
};

struct rk3506_can_data {
	struct can_driver_data common;
	struct k_mutex lock;
	struct k_sem tx_idle;
	struct k_work_delayable tx_timeout_work;
	struct can_timing timing;
	struct rk3506_can_rx_filter filters[CONFIG_CAN_RK3506_MAX_FILTERS];
	can_tx_callback_t tx_callback;
	void *tx_user_data;
	enum can_state state;
	const struct device *dev;
	bool tx_busy;
};

static inline uint32_t rk3506_can_read(const struct device *dev, uint32_t offset)
{
	const struct rk3506_can_config *config = dev->config;

	return sys_read32(config->base + offset);
}

static inline void rk3506_can_write(const struct device *dev, uint32_t offset, uint32_t value)
{
	const struct rk3506_can_config *config = dev->config;

	sys_write32(value, config->base + offset);
}

static int rk3506_can_prepare_resources(const struct device *dev)
{
	const struct rk3506_can_config *config = dev->config;
	int ret;

	for (size_t i = 0U; i < config->clock_count; i++) {
		const struct rk3506_can_clock *clock = &config->clocks[i];

		if (!device_is_ready(clock->dev)) {
			return -ENODEV;
		}

		if (clock->rate != 0U) {
			uint32_t rate = clock->rate;

			ret = clock_control_set_rate(clock->dev, clock->id, &rate);
			if (ret != 0 && ret != -EALREADY) {
				return ret;
			}
		}

		ret = clock_control_on(clock->dev, clock->id);
		if (ret != 0 && ret != -EALREADY) {
			return ret;
		}
	}

	for (size_t i = 0U; i < config->reset_count; i++) {
		const struct reset_dt_spec *reset = &config->resets[i];

		if (!device_is_ready(reset->dev)) {
			return -ENODEV;
		}

		ret = reset_line_deassert_dt(reset);
		if (ret != 0) {
			return ret;
		}
	}

	ret = pinctrl_apply_state(config->pincfg, PINCTRL_STATE_DEFAULT);

	if (ret != 0) {
		return ret;
	}

	k_busy_wait(10U);
	return 0;
}

static int rk3506_can_encode_nbtp(const struct can_timing *timing, uint32_t *nbtp,
				  can_mode_t mode)
{
	uint32_t brp;
	uint32_t sjw;
	uint32_t tseg1;
	uint32_t tseg2;

	ARG_UNUSED(mode);

	if (timing->prescaler < 2U || (timing->prescaler & 1U) != 0U ||
	    timing->sjw < 1U || timing->prop_seg + timing->phase_seg1 < 1U ||
	    timing->phase_seg2 < 1U) {
		return -EINVAL;
	}

	brp = (timing->prescaler / 2U) - 1U;
	sjw = timing->sjw - 1U;
	tseg1 = timing->prop_seg + timing->phase_seg1 - 1U;
	tseg2 = timing->phase_seg2 - 1U;

	if (brp > 0xffU || sjw > 0x7fU || tseg1 > 0xffU || tseg2 > 0x7fU) {
		return -EINVAL;
	}

	*nbtp = (brp << RK3506_CAN_NBTP_NBRP_SHIFT) |
		(sjw << RK3506_CAN_NBTP_NSJW_SHIFT) |
		(tseg1 << RK3506_CAN_NBTP_NTSEG1_SHIFT) |
		(tseg2 << RK3506_CAN_NBTP_NTSEG2_SHIFT);

	return 0;
}

static uint16_t rk3506_can_default_sample_point(uint32_t bitrate)
{
	/*
	 * Match Zephyr common CAN defaults so devicetree sample-point = <0>
	 * behaves as users expect.
	 */
	if (bitrate > 800000U) {
		return 750U;
	}
	if (bitrate > 500000U) {
		return 800U;
	}

	return 875U;
}

static int rk3506_can_update_sample_point(uint32_t total_tq, uint16_t sample_point,
					  const struct can_timing *min,
					  const struct can_timing *max,
					  struct can_timing *candidate)
{
	uint32_t tseg1_min = min->prop_seg + min->phase_seg1;
	uint32_t tseg1_max = max->prop_seg + max->phase_seg1;
	uint32_t tseg1;
	uint32_t tseg2;
	uint32_t sample_point_result;

	tseg2 = total_tq - (total_tq * sample_point) / 1000U;
	tseg2 = CLAMP(tseg2, min->phase_seg2, max->phase_seg2);
	if (total_tq <= RK3506_CAN_SYNC_SEG + tseg2) {
		return -ENOTSUP;
	}

	tseg1 = total_tq - RK3506_CAN_SYNC_SEG - tseg2;
	if (tseg1 > tseg1_max) {
		tseg1 = tseg1_max;
		if (total_tq <= RK3506_CAN_SYNC_SEG + tseg1) {
			return -ENOTSUP;
		}
		tseg2 = total_tq - RK3506_CAN_SYNC_SEG - tseg1;
		if (tseg2 > max->phase_seg2) {
			return -ENOTSUP;
		}
	} else if (tseg1 < tseg1_min) {
		tseg1 = tseg1_min;
		if (total_tq <= RK3506_CAN_SYNC_SEG + tseg1) {
			return -ENOTSUP;
		}
		tseg2 = total_tq - RK3506_CAN_SYNC_SEG - tseg1;
		if (tseg2 < min->phase_seg2) {
			return -ENOTSUP;
		}
	}

	candidate->phase_seg2 = tseg2;
	candidate->prop_seg = CLAMP(tseg1 / 2U, min->prop_seg, max->prop_seg);
	candidate->phase_seg1 = tseg1 - candidate->prop_seg;
	if (candidate->phase_seg1 > max->phase_seg1) {
		candidate->phase_seg1 = max->phase_seg1;
		candidate->prop_seg = tseg1 - candidate->phase_seg1;
	} else if (candidate->phase_seg1 < min->phase_seg1) {
		candidate->phase_seg1 = min->phase_seg1;
		candidate->prop_seg = tseg1 - candidate->phase_seg1;
	}

	sample_point_result = (RK3506_CAN_SYNC_SEG + tseg1) * 1000U / total_tq;

	return sample_point_result > sample_point ?
	       (int)(sample_point_result - sample_point) :
	       (int)(sample_point - sample_point_result);
}

static int rk3506_can_calc_timing(const struct device *dev, struct can_timing *result,
				  uint32_t bitrate, uint16_t sample_point,
				  const struct can_timing *min,
				  const struct can_timing *max)
{
	const struct rk3506_can_config *config = dev->config;
	struct can_timing candidate = { 0 };
	uint32_t max_total_tq = RK3506_CAN_SYNC_SEG + max->prop_seg + max->phase_seg1 +
				max->phase_seg2;
	uint32_t start_prescaler;
	uint32_t nbtp;
	int best_error = INT_MAX;

	if (bitrate == 0U || sample_point >= 1000U) {
		return -EINVAL;
	}
	if (sample_point == 0U) {
		sample_point = rk3506_can_default_sample_point(bitrate);
	}

	start_prescaler = MAX(config->clock_frequency / (max_total_tq * bitrate),
			      min->prescaler);
	if ((start_prescaler & 1U) != 0U) {
		start_prescaler++;
	}

	for (uint32_t prescaler = start_prescaler; prescaler <= max->prescaler;
	     prescaler += 2U) {
		uint32_t total_tq;
		int error;

		if (config->clock_frequency % (prescaler * bitrate) != 0U) {
			continue;
		}

		total_tq = config->clock_frequency / (prescaler * bitrate);
		error = rk3506_can_update_sample_point(total_tq, sample_point,
						       min, max, &candidate);
		if (error < 0) {
			continue;
		}

		candidate.prescaler = prescaler;
		candidate.sjw = MIN(candidate.phase_seg1, candidate.phase_seg2 / 2U);
		candidate.sjw = CLAMP(candidate.sjw, min->sjw, max->sjw);

		if (rk3506_can_encode_nbtp(&candidate, &nbtp, CAN_MODE_NORMAL) != 0) {
			continue;
		}

		if (error < best_error) {
			*result = candidate;
			best_error = error;
			if (error == 0) {
				break;
			}
		}
	}

	return best_error == INT_MAX ? -ENOTSUP : best_error;
}

static int rk3506_can_init_timing(const struct device *dev)
{
	const struct rk3506_can_config *config = dev->config;
	struct rk3506_can_data *data = dev->data;
	uint32_t nbtp;
	int ret;

	ret = rk3506_can_calc_timing(dev, &data->timing, config->common.bitrate,
				     config->common.sample_point,
				     &(const struct can_timing){
					     .sjw = 1U,
					     .prop_seg = 1U,
					     .phase_seg1 = 1U,
					     .phase_seg2 = 1U,
					     .prescaler = RK3506_CAN_TIMING_PRESC_MIN,
				     },
				     &(const struct can_timing){
					     .sjw = RK3506_CAN_TIMING_SJW_MAX,
					     .prop_seg = RK3506_CAN_TIMING_TSEG1_MAX,
					     .phase_seg1 = RK3506_CAN_TIMING_TSEG1_MAX,
					     .phase_seg2 = RK3506_CAN_TIMING_TSEG2_MAX,
					     .prescaler = RK3506_CAN_TIMING_PRESC_MAX,
				     });
	if (ret < 0) {
		LOG_ERR("unsupported CAN%u timing bitrate=%u sample_point=%u clock=%u",
			config->instance, config->common.bitrate,
			config->common.sample_point, config->clock_frequency);
		return ret;
	}

	(void)rk3506_can_encode_nbtp(&data->timing, &nbtp, data->common.mode);
	LOG_INF("RK3506 CAN%u timing bitrate=%u sp=%u err=%d sjw=%u prop=%u ph1=%u "
		"ph2=%u prescaler=%u nbtp=%#x",
		config->instance, config->common.bitrate, config->common.sample_point,
		ret, data->timing.sjw, data->timing.prop_seg, data->timing.phase_seg1,
		data->timing.phase_seg2, data->timing.prescaler, nbtp);

	return 0;
}

static uint32_t rk3506_can_mode_bits(const struct device *dev)
{
	struct rk3506_can_data *data = dev->data;
	uint32_t mode = RK3506_CAN_MODE_WORK;

	if ((data->common.mode & CAN_MODE_LOOPBACK) != 0U) {
		mode |= RK3506_CAN_MODE_LBACK;
	}

	return mode;
}

static bool rk3506_can_take_tx(const struct device *dev, can_tx_callback_t *cb,
			       void **cb_arg)
{
	struct rk3506_can_data *data = dev->data;
	unsigned int key;
	bool active = false;

	*cb = NULL;
	*cb_arg = NULL;

	key = irq_lock();
	if (data->tx_busy) {
		*cb = data->tx_callback;
		*cb_arg = data->tx_user_data;
		data->tx_busy = false;
		data->tx_callback = NULL;
		data->tx_user_data = NULL;
		active = true;
	}
	irq_unlock(key);

	if (active) {
		k_sem_give(&data->tx_idle);
	}

	return active;
}

static enum can_state rk3506_can_state_from_hw(const struct device *dev,
					       struct can_bus_err_cnt *err_cnt)
{
	struct rk3506_can_data *data = dev->data;
	uint32_t state = rk3506_can_read(dev, RK3506_CAN_STATE);
	uint32_t txerr = rk3506_can_read(dev, RK3506_CAN_TXERRORCNT);
	uint32_t rxerr = rk3506_can_read(dev, RK3506_CAN_RXERRORCNT);
	uint32_t maxerr = MAX(txerr, rxerr);

	if (err_cnt != NULL) {
		err_cnt->tx_err_cnt = MIN(txerr, UINT8_MAX);
		err_cnt->rx_err_cnt = MIN(rxerr, UINT8_MAX);
	}

	if (!data->common.started) {
		return CAN_STATE_STOPPED;
	}
	if ((state & RK3506_CAN_STATE_BUS_OFF) != 0U || maxerr >= 256U) {
		return CAN_STATE_BUS_OFF;
	}
	if (maxerr >= 128U) {
		return CAN_STATE_ERROR_PASSIVE;
	}
	if ((state & RK3506_CAN_STATE_ERR_WARN) != 0U || maxerr >= 96U) {
		return CAN_STATE_ERROR_WARNING;
	}

	return CAN_STATE_ERROR_ACTIVE;
}

static void rk3506_can_notify_state(const struct device *dev)
{
	struct rk3506_can_data *data = dev->data;
	struct can_bus_err_cnt err_cnt = { 0 };
	enum can_state state;

	state = rk3506_can_state_from_hw(dev, &err_cnt);
	if (state == data->state) {
		return;
	}

	data->state = state;
	if (data->common.state_change_cb != NULL) {
		data->common.state_change_cb(dev, state, err_cnt,
					     data->common.state_change_cb_user_data);
	}
}

static void rk3506_can_accept_all(const struct device *dev)
{
	for (uint32_t off = RK3506_CAN_ATF0; off < RK3506_CAN_ATF0 + 0x14U; off += 4U) {
		rk3506_can_write(dev, off, 0U);
	}
	for (uint32_t off = RK3506_CAN_ATFM0; off < RK3506_CAN_ATFM0 + 0x14U; off += 4U) {
		rk3506_can_write(dev, off, 0x00007fffU);
	}
	rk3506_can_write(dev, RK3506_CAN_ATF_DLC, 0U);
	rk3506_can_write(dev, RK3506_CAN_ATF_CTL, 0U);
}

static int rk3506_can_configure_core(const struct device *dev)
{
	struct rk3506_can_data *data = dev->data;
	uint32_t nbtp;
	int ret;

	ret = rk3506_can_encode_nbtp(&data->timing, &nbtp, data->common.mode);
	if (ret != 0) {
		return ret;
	}

	rk3506_can_write(dev, RK3506_CAN_MODE, 0U);
	rk3506_can_write(dev, RK3506_CAN_CMD, 0U);
	rk3506_can_write(dev, RK3506_CAN_INT_MASK, 0xffffffffU);
	rk3506_can_write(dev, RK3506_CAN_INT, RK3506_CAN_INT_ALL_WRITABLE);

	rk3506_can_accept_all(dev);

	rk3506_can_write(dev, RK3506_CAN_STR_CTL,
			 RK3506_CAN_STR_CTL_RX_RESET |
			 RK3506_CAN_STR_CTL_ISM_CANFD_FIXED |
			 RK3506_CAN_STR_CTL_TIMEOUT);
	rk3506_can_write(dev, RK3506_CAN_STR_CTL,
			 RK3506_CAN_STR_CTL_ISM_CANFD_FIXED |
			 RK3506_CAN_STR_CTL_TIMEOUT);
	rk3506_can_write(dev, RK3506_CAN_STR_WTM, RK3506_CAN_STR_WTM_CANFD);

	rk3506_can_write(dev, RK3506_CAN_DMA_CTRL, 0U);
	rk3506_can_write(dev, RK3506_CAN_NBTP, nbtp);

	rk3506_can_write(dev, RK3506_CAN_DBTP, 0U);
	rk3506_can_write(dev, RK3506_CAN_TDCR, 0U);

	rk3506_can_write(dev, RK3506_CAN_BRS_CFG, 0x00000007U);
	rk3506_can_write(dev, RK3506_CAN_RXINT_CTRL, 0x00000100U);
	rk3506_can_write(dev, RK3506_CAN_RXINT_TIMEOUT, 0x0000ffffU);
	rk3506_can_write(dev, RK3506_CAN_AUTO_RETX_CFG,
			 RK3506_CAN_AUTO_RETX_EN | RK3506_CAN_RETX_LIMIT_EN |
			 (RK3506_CAN_RETX_TIME_LIMIT_CNT << RK3506_CAN_RETX_TIME_LIMIT_SHIFT));
	rk3506_can_write(dev, RK3506_CAN_BUSOFFRCY_CFG,
			 RK3506_CAN_BUSOFF_RCY_MODE_EN | RK3506_CAN_BUSOFF_RCY_CNT_FAST);
	rk3506_can_write(dev, RK3506_CAN_BUSOFF_RCY_THR, RK3506_CAN_BUSOFF_RCY_TIME_FAST);
	rk3506_can_write(dev, RK3506_CAN_ERROR_MASK,
			 ((data->common.mode & CAN_MODE_LOOPBACK) != 0U) ?
			 RK3506_CAN_ERROR_MASK_ACK : 0U);

	return 0;
}

static int rk3506_can_get_capabilities(const struct device *dev, can_mode_t *cap)
{
	ARG_UNUSED(dev);

	*cap = CAN_MODE_NORMAL | CAN_MODE_LOOPBACK;

	return 0;
}

static int rk3506_can_set_mode(const struct device *dev, can_mode_t mode)
{
	struct rk3506_can_data *data = dev->data;
	can_mode_t supported;

	(void)rk3506_can_get_capabilities(dev, &supported);
	if ((mode & ~supported) != 0U) {
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	if (data->common.started) {
		k_mutex_unlock(&data->lock);
		return -EBUSY;
	}

	data->common.mode = mode;
	k_mutex_unlock(&data->lock);

	return 0;
}

static int rk3506_can_set_timing(const struct device *dev, const struct can_timing *timing)
{
	struct rk3506_can_data *data = dev->data;
	uint32_t ignored;
	int ret;

	ret = rk3506_can_encode_nbtp(timing, &ignored, data->common.mode);
	if (ret != 0) {
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	if (data->common.started) {
		k_mutex_unlock(&data->lock);
		return -EBUSY;
	}

	data->timing = *timing;
	k_mutex_unlock(&data->lock);

	return 0;
}

static int rk3506_can_start(const struct device *dev)
{
	const struct rk3506_can_config *config = dev->config;
	struct rk3506_can_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	if (data->common.started) {
		k_mutex_unlock(&data->lock);
		return -EALREADY;
	}

	ret = rk3506_can_prepare_resources(dev);
	if (ret != 0) {
		k_mutex_unlock(&data->lock);
		return ret;
	}

	ret = rk3506_can_configure_core(dev);
	if (ret != 0) {
		k_mutex_unlock(&data->lock);
		return ret;
	}

	rk3506_can_write(dev, RK3506_CAN_INT_MASK, BIT(0));
	rk3506_can_write(dev, RK3506_CAN_MODE, rk3506_can_mode_bits(dev));
	config->irq_config_func(dev);

	data->state = CAN_STATE_ERROR_ACTIVE;
	data->common.started = true;
	k_mutex_unlock(&data->lock);

	return 0;
}

static int rk3506_can_stop(const struct device *dev)
{
	struct rk3506_can_data *data = dev->data;
	can_tx_callback_t cb;
	void *cb_arg;

	k_mutex_lock(&data->lock, K_FOREVER);
	if (!data->common.started) {
		k_mutex_unlock(&data->lock);
		return -EALREADY;
	}

	rk3506_can_write(dev, RK3506_CAN_MODE, 0U);
	rk3506_can_write(dev, RK3506_CAN_CMD, 0U);
	rk3506_can_write(dev, RK3506_CAN_INT_MASK, 0xffffffffU);
	rk3506_can_write(dev, RK3506_CAN_INT, RK3506_CAN_INT_ALL_WRITABLE);
	(void)k_work_cancel_delayable(&data->tx_timeout_work);
	data->common.started = false;
	data->state = CAN_STATE_STOPPED;
	k_mutex_unlock(&data->lock);

	if (rk3506_can_take_tx(dev, &cb, &cb_arg)) {
		if (cb != NULL) {
			cb(dev, -ENETDOWN, cb_arg);
		}
	}

	return 0;
}

static uint32_t rk3506_can_next_tx_cmd(const struct device *dev)
{
	if ((rk3506_can_read(dev, RK3506_CAN_CMD) & RK3506_CAN_CMD_TX0_REQ) != 0U) {
		return RK3506_CAN_CMD_TX1_REQ;
	}

	return RK3506_CAN_CMD_TX0_REQ;
}

static int rk3506_can_send(const struct device *dev, const struct can_frame *frame,
			   k_timeout_t timeout, can_tx_callback_t callback, void *user_data)
{
	struct rk3506_can_data *data = dev->data;
	uint32_t txfic = frame->dlc & RK3506_CAN_TX_DLC_MASK;
	uint32_t data_len;
	uint32_t cmd;
	enum can_state state;
	int ret;

	if (frame->flags != 0U) {
		return -ENOTSUP;
	}

	if (frame->id > CAN_STD_ID_MASK) {
		return -EINVAL;
	}
	if (frame->dlc > CAN_MAX_DLC) {
		return -EINVAL;
	}

	data_len = can_dlc_to_bytes(frame->dlc);

	ret = k_sem_take(&data->tx_idle, timeout);
	if (ret != 0) {
		return -EAGAIN;
	}

	if (!k_is_in_isr()) {
		struct k_work_sync sync;

		(void)k_work_cancel_delayable_sync(&data->tx_timeout_work, &sync);
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	if (!data->common.started) {
		k_mutex_unlock(&data->lock);
		k_sem_give(&data->tx_idle);
		return -ENETDOWN;
	}
	state = rk3506_can_state_from_hw(dev, NULL);
	if (state == CAN_STATE_BUS_OFF) {
		k_mutex_unlock(&data->lock);
		k_sem_give(&data->tx_idle);
		return -ENETUNREACH;
	}

	data->tx_busy = true;
	data->tx_callback = callback;
	data->tx_user_data = user_data;

	rk3506_can_write(dev, RK3506_CAN_INT, RK3506_CAN_INT_TX_FINISH |
			 RK3506_CAN_INT_AUTO_RETX_FAIL | RK3506_CAN_INT_BUS_OFF |
			 RK3506_CAN_INT_BUS_ERR);
	rk3506_can_write(dev, RK3506_CAN_TXID, frame->id);
	rk3506_can_write(dev, RK3506_CAN_TXFIC, txfic);
	for (uint32_t off = 0U; off < ROUND_UP(data_len, 4U); off += 4U) {
		uint32_t word = 0U;

		memcpy(&word, &frame->data[off], MIN(4U, data_len - off));
		rk3506_can_write(dev, RK3506_CAN_TXDAT0 + off, word);
	}

	cmd = rk3506_can_next_tx_cmd(dev);
	rk3506_can_write(dev, RK3506_CAN_CMD, cmd);
	(void)k_work_reschedule(&data->tx_timeout_work, K_MSEC(1000));
	k_mutex_unlock(&data->lock);

	return 0;
}

static int rk3506_can_add_rx_filter(const struct device *dev, can_rx_callback_t callback,
				    void *user_data, const struct can_filter *filter)
{
	struct rk3506_can_data *data = dev->data;
	int filter_id = -ENOSPC;
	unsigned int key;

	if (callback == NULL || filter == NULL) {
		return -EINVAL;
	}
	if (filter->flags != 0U) {
		return -ENOTSUP;
	}
	if (filter->id > CAN_STD_ID_MASK || filter->mask > CAN_STD_ID_MASK) {
		return -EINVAL;
	}

	key = irq_lock();
	for (int i = 0; i < ARRAY_SIZE(data->filters); i++) {
		if (data->filters[i].callback == NULL) {
			data->filters[i].callback = callback;
			data->filters[i].user_data = user_data;
			data->filters[i].filter = *filter;
			filter_id = i;
			break;
		}
	}
	irq_unlock(key);

	return filter_id;
}

static void rk3506_can_remove_rx_filter(const struct device *dev, int filter_id)
{
	struct rk3506_can_data *data = dev->data;
	unsigned int key;

	if (filter_id < 0 || filter_id >= ARRAY_SIZE(data->filters)) {
		return;
	}

	key = irq_lock();
	data->filters[filter_id].callback = NULL;
	data->filters[filter_id].user_data = NULL;
	memset(&data->filters[filter_id].filter, 0, sizeof(data->filters[filter_id].filter));
	irq_unlock(key);
}

static int rk3506_can_get_state(const struct device *dev, enum can_state *state,
				struct can_bus_err_cnt *err_cnt)
{
	if (state != NULL) {
		*state = rk3506_can_state_from_hw(dev, err_cnt);
	} else if (err_cnt != NULL) {
		(void)rk3506_can_state_from_hw(dev, err_cnt);
	}

	return 0;
}

static void rk3506_can_set_state_change_callback(const struct device *dev,
						 can_state_change_callback_t callback,
						 void *user_data)
{
	struct rk3506_can_data *data = dev->data;

	data->common.state_change_cb = callback;
	data->common.state_change_cb_user_data = user_data;
}

static int rk3506_can_get_core_clock(const struct device *dev, uint32_t *rate)
{
	const struct rk3506_can_config *config = dev->config;

	*rate = config->clock_frequency;

	return 0;
}

static int rk3506_can_get_max_filters(const struct device *dev, bool ide)
{
	ARG_UNUSED(dev);

	if (ide) {
		return 0;
	}

	return CONFIG_CAN_RK3506_MAX_FILTERS;
}

static void rk3506_can_dispatch_rx(const struct device *dev, struct can_frame *frame)
{
	struct rk3506_can_data *data = dev->data;

	for (int i = 0; i < ARRAY_SIZE(data->filters); i++) {
		if (data->filters[i].callback != NULL &&
		    can_frame_matches_filter(frame, &data->filters[i].filter)) {
			struct can_frame tmp = *frame;

			data->filters[i].callback(dev, &tmp, data->filters[i].user_data);
		}
	}
}

static void rk3506_can_rx_one(const struct device *dev)
{
	uint32_t fic = rk3506_can_read(dev, RK3506_CAN_RXFRD);
	uint32_t id = rk3506_can_read(dev, RK3506_CAN_RXFRD);
	struct can_frame frame = { 0 };
	bool unsupported;
	uint8_t data_len;
	uint32_t data_words;

	frame.dlc = (fic & RK3506_CAN_RX_DLC_MASK) >> RK3506_CAN_RX_DLC_SHIFT;
	frame.id = id & CAN_STD_ID_MASK;

	unsupported = (frame.dlc > CAN_MAX_DLC) ||
		      ((fic & (RK3506_CAN_RX_FORMAT_EXT |
			       RK3506_CAN_RX_RTR |
			       RK3506_CAN_RX_FDF |
			       RK3506_CAN_RX_BRS)) != 0U);
	data_len = unsupported ? 0U : can_dlc_to_bytes(frame.dlc);
	data_words = ROUND_UP(data_len, 4U) / 4U;

	for (uint32_t word_idx = 0U; word_idx < 16U; word_idx++) {
		uint32_t word = rk3506_can_read(dev, RK3506_CAN_RXFRD);

		if (!unsupported && word_idx < data_words) {
			uint32_t off = word_idx * 4U;

			memcpy(&frame.data[off], &word, MIN(4U, data_len - off));
		}
	}

	if (unsupported) {
		return;
	}

	rk3506_can_dispatch_rx(dev, &frame);
}

static void rk3506_can_drain_rx(const struct device *dev)
{
	uint32_t str = rk3506_can_read(dev, RK3506_CAN_STR_STATE);
	uint32_t left = (str & RK3506_CAN_STR_INTM_LEFT_MASK) >> RK3506_CAN_STR_INTM_LEFT_SHIFT;
	uint32_t cnt = (str & RK3506_CAN_STR_INTM_CNT_MASK) >> RK3506_CAN_STR_INTM_CNT_SHIFT;
	uint32_t quota = left / RK3506_CAN_RX_WORDS;

	if (quota != cnt && cnt != 0U) {
		quota = cnt;
	}
	if ((str & RK3506_CAN_STR_INTM_EMPTY) != 0U && quota == 0U) {
		return;
	}

	for (uint32_t i = 0U; i < MIN(quota, 16U); i++) {
		rk3506_can_rx_one(dev);
	}
}

static void rk3506_can_finish_tx(const struct device *dev, int error)
{
	struct rk3506_can_data *data = dev->data;
	can_tx_callback_t cb;
	void *cb_arg;

	(void)k_work_cancel_delayable(&data->tx_timeout_work);
	if (rk3506_can_take_tx(dev, &cb, &cb_arg) && cb != NULL) {
		cb(dev, error, cb_arg);
	}
}

static void rk3506_can_tx_timeout_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct rk3506_can_data *data = CONTAINER_OF(dwork, struct rk3506_can_data,
						    tx_timeout_work);
	const struct device *dev = data->dev;
	bool tx_busy = false;
	can_tx_callback_t cb;
	void *cb_arg;

	if (dev == NULL) {
		return;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	if (data->tx_busy) {
		rk3506_can_write(dev, RK3506_CAN_CMD, 0U);
		tx_busy = true;
	}
	k_mutex_unlock(&data->lock);

	if (tx_busy) {
		if (rk3506_can_take_tx(dev, &cb, &cb_arg) && cb != NULL) {
			cb(dev, -EIO, cb_arg);
		}
	}
}

static void rk3506_can_isr(const struct device *dev)
{
	uint32_t isr = rk3506_can_read(dev, RK3506_CAN_INT);

	if (isr == 0U) {
		return;
	}

	if ((isr & RK3506_CAN_INT_RX_MASK) != 0U) {
		rk3506_can_write(dev, RK3506_CAN_INT_MASK,
				 RK3506_CAN_INT_ISM_WTM |
				 RK3506_CAN_INT_RX_STR_TIMEOUT |
				 RK3506_CAN_INT_RX_FINISH);
		rk3506_can_drain_rx(dev);
		rk3506_can_write(dev, RK3506_CAN_INT_MASK, BIT(0));
	}

	if ((isr & RK3506_CAN_INT_TX_FINISH) != 0U) {
		rk3506_can_write(dev, RK3506_CAN_CMD, 0U);
		rk3506_can_finish_tx(dev, 0);
	} else if ((isr & (RK3506_CAN_INT_AUTO_RETX_FAIL |
			   RK3506_CAN_INT_BUS_OFF |
			   RK3506_CAN_INT_BUS_ERR)) != 0U) {
		rk3506_can_finish_tx(dev, -EIO);
	}

	if ((isr & RK3506_CAN_INT_ERROR_MASK) != 0U) {
		rk3506_can_notify_state(dev);
	}

	rk3506_can_write(dev, RK3506_CAN_INT, isr);
}

static int rk3506_can_init(const struct device *dev)
{
	const struct rk3506_can_config *config = dev->config;
	struct rk3506_can_data *data = dev->data;
	int ret;

	k_mutex_init(&data->lock);
	k_sem_init(&data->tx_idle, 1, 1);
	k_work_init_delayable(&data->tx_timeout_work, rk3506_can_tx_timeout_work_handler);
	data->dev = dev;
	data->common.mode = CAN_MODE_NORMAL;
	data->state = CAN_STATE_STOPPED;

	ret = rk3506_can_init_timing(dev);
	if (ret != 0) {
		return ret;
	}

	ret = rk3506_can_prepare_resources(dev);
	if (ret != 0) {
		return ret;
	}

	rk3506_can_write(dev, RK3506_CAN_MODE, 0U);
	rk3506_can_write(dev, RK3506_CAN_CMD, 0U);
	rk3506_can_write(dev, RK3506_CAN_INT_MASK, 0xffffffffU);
	rk3506_can_write(dev, RK3506_CAN_INT, RK3506_CAN_INT_ALL_WRITABLE);

	LOG_INF("RK3506 CAN%u init base=%#lx clock=%u", config->instance,
		(unsigned long)config->base, config->clock_frequency);

	return 0;
}

static DEVICE_API(can, rk3506_can_api) = {
	.get_capabilities = rk3506_can_get_capabilities,
	.start = rk3506_can_start,
	.stop = rk3506_can_stop,
	.set_mode = rk3506_can_set_mode,
	.set_timing = rk3506_can_set_timing,
	.send = rk3506_can_send,
	.add_rx_filter = rk3506_can_add_rx_filter,
	.remove_rx_filter = rk3506_can_remove_rx_filter,
	.get_state = rk3506_can_get_state,
	.set_state_change_callback = rk3506_can_set_state_change_callback,
	.get_core_clock = rk3506_can_get_core_clock,
	.get_max_filters = rk3506_can_get_max_filters,
	.timing_min = {
		.sjw = 1U,
		.prop_seg = 1U,
		.phase_seg1 = 1U,
		.phase_seg2 = 1U,
		.prescaler = 2U,
	},
	.timing_max = {
		.sjw = 128U,
		.prop_seg = 128U,
		.phase_seg1 = 128U,
		.phase_seg2 = 128U,
		.prescaler = 256U,
	},
};

#define RK3506_CAN_IRQ_CONFIG_FUNC(inst) \
	static void rk3506_can_irq_config_func_##inst(const struct device *dev) \
	{ \
		ARG_UNUSED(dev); \
		IRQ_CONNECT(DT_INST_IRQN(inst), DT_INST_IRQ(inst, priority), \
			    rk3506_can_isr, DEVICE_DT_INST_GET(inst), 0); \
		irq_enable(DT_INST_IRQN(inst)); \
	}

#define RK3506_CAN_CLOCK_INIT(inst) \
	static const struct rk3506_can_clock rk3506_can_clocks_##inst[] = { \
		{ \
			.dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_NAME(inst, baudclk)), \
			.id = (clock_control_subsys_t)(uintptr_t) \
				DT_INST_CLOCKS_CELL_BY_NAME(inst, baudclk, id), \
			.rate = DT_INST_PROP(inst, clock_frequency), \
		}, \
		{ \
			.dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_NAME(inst, apb_pclk)), \
			.id = (clock_control_subsys_t)(uintptr_t) \
				DT_INST_CLOCKS_CELL_BY_NAME(inst, apb_pclk, id), \
			.rate = 0U, \
		}, \
	}

#define RK3506_CAN_RESET_INIT(inst) \
	static const struct reset_dt_spec rk3506_can_resets_##inst[] = { \
		RESET_DT_SPEC_INST_GET_BY_IDX(inst, 0), \
		RESET_DT_SPEC_INST_GET_BY_IDX(inst, 1), \
	}

#define RK3506_CAN_INIT(inst) \
	RK3506_CAN_IRQ_CONFIG_FUNC(inst) \
	RK3506_CAN_CLOCK_INIT(inst); \
	RK3506_CAN_RESET_INIT(inst); \
	PINCTRL_DT_INST_DEFINE(inst); \
	\
	static struct rk3506_can_data rk3506_can_data_##inst; \
	\
	static const struct rk3506_can_config rk3506_can_config_##inst = { \
		.common = CAN_DT_DRIVER_CONFIG_INST_GET(inst, 10000U, 1000000U), \
		.pincfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst), \
		.clocks = rk3506_can_clocks_##inst, \
		.clock_count = ARRAY_SIZE(rk3506_can_clocks_##inst), \
		.resets = rk3506_can_resets_##inst, \
		.reset_count = ARRAY_SIZE(rk3506_can_resets_##inst), \
		.base = DT_INST_REG_ADDR(inst), \
		.clock_frequency = DT_INST_PROP(inst, clock_frequency), \
		.irq_config_func = rk3506_can_irq_config_func_##inst, \
		.instance = inst, \
	}; \
	\
	CAN_DEVICE_DT_INST_DEFINE(inst, rk3506_can_init, NULL, \
				  &rk3506_can_data_##inst, \
				  &rk3506_can_config_##inst, POST_KERNEL, \
				  CONFIG_CAN_INIT_PRIORITY, &rk3506_can_api);

DT_INST_FOREACH_STATUS_OKAY(RK3506_CAN_INIT)
