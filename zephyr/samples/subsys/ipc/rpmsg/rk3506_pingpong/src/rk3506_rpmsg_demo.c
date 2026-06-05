/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/can.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "rk3506_rpmsg_demo.h"
#include "rk3506_amp_layout.h"
#include "rk3506_rpmsg_platform.h"
#include "rpmsg_lite.h"
#include "rpmsg_ns.h"

#define RK3506_RPMSG_PING_PREFIX "ping:"
#define RK3506_RPMSG_PONG_PREFIX "pong:"
#define RK3506_RPMSG_CAN_TXRX_PREFIX "can:txrx:"
#define RK3506_RPMSG_CAN_TX_PREFIX "can:tx:"
#define RK3506_RPMSG_CAN_RX_PREFIX "can:rx:"
#define RK3506_RPMSG_REPLY_BUF_SIZE 512U
#define RK3506_RPMSG_CMD_BUF_SIZE 128U

#define RK3506_RPMSG_THREAD_STACK 4096
#define RK3506_RPMSG_THREAD_PRIO 5
#define RK3506_CAN_THREAD_STACK 4096
#define RK3506_CAN_THREAD_PRIO 6

#if defined(CONFIG_CAN) && DT_HAS_CHOSEN(zephyr_canbus) && \
	DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_canbus), okay)
#define RK3506_RPMSG_CAN_API 1
#define RK3506_CAN_DEV DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus))
#endif

enum rk3506_can_rpmsg_op {
	RK3506_CAN_RPMSG_TX,
	RK3506_CAN_RPMSG_RX,
	RK3506_CAN_RPMSG_TXRX,
};

struct rk3506_can_rpmsg_req {
	uint32_t src;
	uint32_t id;
	uint32_t mask;
	uint32_t timeout_ms;
	uint32_t repeat;
	uint32_t gap_us;
	enum rk3506_can_rpmsg_op op;
	uint8_t len;
	uint8_t data[8];
};

static struct rpmsg_lite_instance *rpmsg_dev;
static struct rpmsg_lite_endpoint *rpmsg_ept;
static uint32_t rpmsg_rx_count;
static uint32_t rpmsg_tx_count;
static uint32_t rpmsg_tx_fail_count;
static uint32_t rpmsg_ns_sent;

K_THREAD_STACK_DEFINE(rk3506_rpmsg_stack, RK3506_RPMSG_THREAD_STACK);
static struct k_thread rk3506_rpmsg_thread;

#if defined(RK3506_RPMSG_CAN_API)
K_MSGQ_DEFINE(rk3506_can_msgq, sizeof(struct rk3506_can_rpmsg_req), 4, 4);
K_THREAD_STACK_DEFINE(rk3506_can_stack, RK3506_CAN_THREAD_STACK);
static struct k_thread rk3506_can_thread;
static K_SEM_DEFINE(rk3506_can_tx_done, 0, 1);
static K_SEM_DEFINE(rk3506_can_rx_done, 0, 1);
static bool rk3506_can_started;
static int rk3506_can_tx_error;
static struct can_frame rk3506_can_rx_frame;

static int rk3506_rpmsg_send_text(uint32_t dst, const char *text, size_t len)
{
	int32_t ret;

	if (rpmsg_dev == NULL || rpmsg_ept == NULL || text == NULL ||
	    len > RK3506_RPMSG_REPLY_BUF_SIZE) {
		rpmsg_tx_fail_count++;
		return -EINVAL;
	}

	ret = rpmsg_lite_send(rpmsg_dev, rpmsg_ept, dst, (char *)text,
			      (uint32_t)len, RL_DONT_BLOCK);
	if (ret == RL_SUCCESS) {
		rpmsg_tx_count++;
		return 0;
	}

	rpmsg_tx_fail_count++;
	printk("[RK3506][RPMSG] send failed: %d\n", ret);
	return -EIO;
}

static int rk3506_parse_u32(const char *s, uint32_t *out)
{
	char *end = NULL;
	unsigned long val;

	if (s == NULL || *s == '\0' || out == NULL) {
		return -EINVAL;
	}

	val = strtoul(s, &end, 0);
	if (end == s || *end != '\0' || val > UINT32_MAX) {
		return -EINVAL;
	}

	*out = (uint32_t)val;
	return 0;
}

static int rk3506_validate_timeout_ms(uint32_t timeout_ms)
{
	return (timeout_ms >= 1U && timeout_ms <= 600000U) ? 0 : -EINVAL;
}

static int rk3506_hex_nibble(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}

	return -EINVAL;
}

static int rk3506_parse_hex_data(const char *s, uint8_t *data, uint8_t *len)
{
	size_t slen;

	if (s == NULL || data == NULL || len == NULL) {
		return -EINVAL;
	}

	slen = strlen(s);
	if ((slen % 2U) != 0U || slen > 16U) {
		return -EINVAL;
	}

	*len = (uint8_t)(slen / 2U);
	for (size_t i = 0U; i < slen; i += 2U) {
		int hi = rk3506_hex_nibble(s[i]);
		int lo = rk3506_hex_nibble(s[i + 1U]);

		if (hi < 0 || lo < 0) {
			return -EINVAL;
		}
		data[i / 2U] = (uint8_t)((hi << 4) | lo);
	}

	return 0;
}

static size_t rk3506_hex_encode(const uint8_t *data, uint8_t len,
				char *out, size_t out_size)
{
	static const char hex[] = "0123456789abcdef";
	size_t needed = (size_t)len * 2U + 1U;

	if (out_size < needed) {
		return 0U;
	}

	for (uint8_t i = 0U; i < len; i++) {
		out[i * 2U] = hex[data[i] >> 4];
		out[i * 2U + 1U] = hex[data[i] & 0xfU];
	}
	out[len * 2U] = '\0';

	return len * 2U;
}

static int rk3506_parse_can_txrx_req(const void *payload, uint32_t payload_len,
				     uint32_t src,
				     struct rk3506_can_rpmsg_req *req)
{
	char msg[RK3506_RPMSG_CMD_BUF_SIZE];
	char *id_s;
	char *data_s;
	char *timeout_s;
	uint32_t timeout_ms = 1000U;

	if (payload_len >= sizeof(msg) || req == NULL) {
		return -EINVAL;
	}

	memcpy(msg, payload, payload_len);
	msg[payload_len] = '\0';

	id_s = msg + strlen(RK3506_RPMSG_CAN_TXRX_PREFIX);
	data_s = strchr(id_s, ':');
	if (data_s == NULL) {
		return -EINVAL;
	}
	*data_s++ = '\0';

	timeout_s = strchr(data_s, ':');
	if (timeout_s != NULL) {
		*timeout_s++ = '\0';
		if (rk3506_parse_u32(timeout_s, &timeout_ms) != 0) {
			return -EINVAL;
		}
	}

	memset(req, 0, sizeof(*req));
	req->src = src;
	req->timeout_ms = timeout_ms;
	req->op = RK3506_CAN_RPMSG_TXRX;

	if (rk3506_parse_u32(id_s, &req->id) != 0 ||
	    req->id > 0x7ffU ||
	    rk3506_parse_hex_data(data_s, req->data, &req->len) != 0 ||
	    rk3506_validate_timeout_ms(timeout_ms) != 0) {
		return -EINVAL;
	}

	return 0;
}

static int rk3506_parse_can_tx_req(const void *payload, uint32_t payload_len,
				   uint32_t src,
				   struct rk3506_can_rpmsg_req *req)
{
	char msg[RK3506_RPMSG_CMD_BUF_SIZE];
	char *id_s;
	char *data_s;
	char *repeat_s;
	char *gap_s;
	uint32_t repeat = 1U;
	uint32_t gap_us = 1000U;

	if (payload_len >= sizeof(msg) || req == NULL) {
		return -EINVAL;
	}

	memcpy(msg, payload, payload_len);
	msg[payload_len] = '\0';

	id_s = msg + strlen(RK3506_RPMSG_CAN_TX_PREFIX);
	data_s = strchr(id_s, ':');
	if (data_s == NULL) {
		return -EINVAL;
	}
	*data_s++ = '\0';

	repeat_s = strchr(data_s, ':');
	if (repeat_s != NULL) {
		*repeat_s++ = '\0';
		gap_s = strchr(repeat_s, ':');
		if (gap_s != NULL) {
			*gap_s++ = '\0';
		}

		if (rk3506_parse_u32(repeat_s, &repeat) != 0) {
			return -EINVAL;
		}
		if (gap_s != NULL && rk3506_parse_u32(gap_s, &gap_us) != 0) {
			return -EINVAL;
		}
	}

	memset(req, 0, sizeof(*req));
	req->src = src;
	req->timeout_ms = 0U;
	req->repeat = repeat;
	req->gap_us = gap_us;
	req->op = RK3506_CAN_RPMSG_TX;

	if (rk3506_parse_u32(id_s, &req->id) != 0 ||
	    req->id > 0x7ffU ||
	    rk3506_parse_hex_data(data_s, req->data, &req->len) != 0 ||
	    repeat == 0U || repeat > 100000U || gap_us > 1000000U) {
		return -EINVAL;
	}

	return 0;
}

static int rk3506_parse_can_rx_req(const void *payload, uint32_t payload_len,
				   uint32_t src,
				   struct rk3506_can_rpmsg_req *req)
{
	char msg[RK3506_RPMSG_CMD_BUF_SIZE];
	char *arg0;
	char *arg1;
	char *arg2;
	uint32_t timeout_ms = 1000U;

	if (payload_len >= sizeof(msg) || req == NULL) {
		return -EINVAL;
	}

	memcpy(msg, payload, payload_len);
	msg[payload_len] = '\0';

	arg0 = msg + strlen(RK3506_RPMSG_CAN_RX_PREFIX);
	if (*arg0 == '\0') {
		return -EINVAL;
	}

	memset(req, 0, sizeof(*req));
	req->src = src;
	req->op = RK3506_CAN_RPMSG_RX;

	arg1 = strchr(arg0, ':');
	if (arg1 == NULL) {
		if (strcmp(arg0, "any") == 0) {
			if (rk3506_validate_timeout_ms(timeout_ms) != 0) {
				return -EINVAL;
			}
			req->timeout_ms = timeout_ms;
			return 0;
		}
		if (rk3506_parse_u32(arg0, &timeout_ms) != 0) {
			return -EINVAL;
		}
		if (rk3506_validate_timeout_ms(timeout_ms) != 0) {
			return -EINVAL;
		}
		req->timeout_ms = timeout_ms;
		return 0;
	}

	*arg1++ = '\0';
	if (strcmp(arg0, "any") == 0) {
		if (rk3506_parse_u32(arg1, &timeout_ms) != 0 ||
		    rk3506_validate_timeout_ms(timeout_ms) != 0) {
			return -EINVAL;
		}
		req->timeout_ms = timeout_ms;
		return 0;
	}

	arg2 = strchr(arg1, ':');
	if (arg2 == NULL) {
		return -EINVAL;
	}
	*arg2++ = '\0';

	if (rk3506_parse_u32(arg0, &req->id) != 0 ||
	    rk3506_parse_u32(arg1, &req->mask) != 0 ||
	    rk3506_parse_u32(arg2, &timeout_ms) != 0 ||
	    req->id > 0x7ffU || req->mask > 0x7ffU ||
	    rk3506_validate_timeout_ms(timeout_ms) != 0) {
		return -EINVAL;
	}

	req->timeout_ms = timeout_ms;
	return 0;
}

static void rk3506_can_tx_callback(const struct device *dev, int error,
				   void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	rk3506_can_tx_error = error;
	k_sem_give(&rk3506_can_tx_done);
}

static int rk3506_can_ensure_started(const struct device *can)
{
	int ret;

	if (!device_is_ready(can)) {
		return -ENODEV;
	}

	if (rk3506_can_started) {
		return 0;
	}

	ret = can_set_mode(can, CAN_MODE_NORMAL);
	if (ret != 0 && ret != -EBUSY) {
		return ret;
	}

	ret = can_start(can);
	if (ret == 0 || ret == -EALREADY) {
		rk3506_can_started = true;
		return 0;
	}

	return ret;
}

static void rk3506_can_rx_callback(const struct device *dev,
				   struct can_frame *frame,
				   void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	rk3506_can_rx_frame = *frame;
	k_sem_give(&rk3506_can_rx_done);
}

static void rk3506_can_worker(void *p1, void *p2, void *p3)
{
	const struct device *can = RK3506_CAN_DEV;
	struct rk3506_can_rpmsg_req req;
	char reply[RK3506_RPMSG_REPLY_BUF_SIZE];
	char data_hex[17];
	int ret;
	int len;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		k_msgq_get(&rk3506_can_msgq, &req, K_FOREVER);
		enum can_state state = CAN_STATE_STOPPED;
		struct can_bus_err_cnt err_cnt = { 0 };
		uint32_t requested = 0U;
		uint32_t txok = 0U;

		rk3506_hex_encode(req.data, req.len, data_hex, sizeof(data_hex));
		if (req.op == RK3506_CAN_RPMSG_TX) {
			struct can_frame frame = {
				.id = req.id,
				.dlc = req.len,
				.flags = 0,
			};

			memcpy(frame.data, req.data, req.len);
			ret = rk3506_can_ensure_started(can);
			for (uint32_t i = 0U; i < req.repeat && ret == 0; i++) {
				k_sem_reset(&rk3506_can_tx_done);
				rk3506_can_tx_error = 0;
				ret = can_send(can, &frame, K_MSEC(1000),
					       rk3506_can_tx_callback, NULL);
				requested++;
				if (ret == 0) {
					ret = k_sem_take(&rk3506_can_tx_done,
							 K_MSEC(1100));
					if (ret == 0) {
						ret = rk3506_can_tx_error;
					}
				}
				if (ret == 0) {
					txok++;
				}
				if (ret != 0) {
					break;
				}
				if (req.gap_us != 0U) {
					(void)k_usleep(req.gap_us);
				}
			}
			(void)can_get_state(can, &state, &err_cnt);
			len = snprintk(reply, sizeof(reply),
				       "can:tx:ret=%d,req=%u,txok=%u,id=0x%03x,"
				       "dlc=%u,data=%s,state=%d,rxerr=%u,txerr=%u",
				       ret, requested, txok, req.id, req.len,
				       data_hex, state, err_cnt.rx_err_cnt,
				       err_cnt.tx_err_cnt);
			printk("[RK3506][CAN-RPMSG] api tx ret=%d id=0x%03x len=%u req=%u txok=%u state=%d rxerr=%u txerr=%u\n",
			       ret, req.id, req.len, requested, txok, state,
			       err_cnt.rx_err_cnt, err_cnt.tx_err_cnt);
		} else if (req.op == RK3506_CAN_RPMSG_RX) {
			struct can_filter filter = {
				.id = req.id,
				.mask = req.mask,
				.flags = 0U,
			};
			char rx_hex[17] = { 0 };
			int filter_id = -1;

			ret = rk3506_can_ensure_started(can);
			if (ret == 0) {
				memset(&rk3506_can_rx_frame, 0,
				       sizeof(rk3506_can_rx_frame));
				k_sem_reset(&rk3506_can_rx_done);
				filter_id = can_add_rx_filter(can,
							      rk3506_can_rx_callback,
							      NULL, &filter);
				if (filter_id < 0) {
					ret = filter_id;
				}
			}
			if (ret == 0) {
				ret = k_sem_take(&rk3506_can_rx_done,
						 K_MSEC(req.timeout_ms));
			}
			if (filter_id >= 0) {
				can_remove_rx_filter(can, filter_id);
			}
			(void)can_get_state(can, &state, &err_cnt);
			if (ret == 0) {
				(void)rk3506_hex_encode(rk3506_can_rx_frame.data,
							MIN(rk3506_can_rx_frame.dlc, 8U),
							rx_hex, sizeof(rx_hex));
			}
			len = snprintk(reply, sizeof(reply),
				       "can:rx:ret=%d,id=0x%03x,dlc=%u,data=%s,"
				       "filter_id=0x%03x,filter_mask=0x%03x,"
				       "state=%d,rxerr=%u,txerr=%u",
				       ret,
				       ret == 0 ? rk3506_can_rx_frame.id : 0U,
				       ret == 0 ? rk3506_can_rx_frame.dlc : 0U,
				       ret == 0 ? rx_hex : "",
				       req.id, req.mask, state,
				       err_cnt.rx_err_cnt, err_cnt.tx_err_cnt);
			printk("[RK3506][CAN-RPMSG] api rx ret=%d filter=0x%03x/0x%03x state=%d rxerr=%u txerr=%u\n",
			       ret, req.id, req.mask, state, err_cnt.rx_err_cnt,
			       err_cnt.tx_err_cnt);
		} else {
			struct can_frame frame = {
				.id = req.id,
				.dlc = req.len,
				.flags = 0,
			};
			struct can_filter filter = {
				.id = 0U,
				.mask = 0U,
				.flags = 0U,
			};
			char rx_hex[17] = { 0 };
			int filter_id = -1;

			memcpy(frame.data, req.data, req.len);
			ret = rk3506_can_ensure_started(can);
			if (ret == 0) {
				k_sem_reset(&rk3506_can_rx_done);
				filter_id = can_add_rx_filter(can, rk3506_can_rx_callback,
							      NULL, &filter);
				if (filter_id < 0) {
					ret = filter_id;
				}
			}
			if (ret == 0) {
				k_sem_reset(&rk3506_can_tx_done);
				rk3506_can_tx_error = 0;
				ret = can_send(can, &frame, K_MSEC(req.timeout_ms),
					       rk3506_can_tx_callback, NULL);
				requested = 1U;
			}
			if (ret == 0) {
				ret = k_sem_take(&rk3506_can_tx_done,
						 K_MSEC(req.timeout_ms + 100U));
				if (ret == 0) {
					ret = rk3506_can_tx_error;
					txok = (ret == 0) ? 1U : 0U;
				}
			}
			if (ret == 0) {
				ret = k_sem_take(&rk3506_can_rx_done,
						 K_MSEC(req.timeout_ms));
			}
			if (filter_id >= 0) {
				can_remove_rx_filter(can, filter_id);
			}
			(void)can_get_state(can, &state, &err_cnt);
			if (ret == 0) {
				(void)rk3506_hex_encode(rk3506_can_rx_frame.data,
							MIN(rk3506_can_rx_frame.dlc, 8U),
							rx_hex, sizeof(rx_hex));
			}
			len = snprintk(reply, sizeof(reply),
				       "can:txrx:ret=%d,txok=%u,tx_id=0x%03x,"
				       "tx_dlc=%u,tx_data=%s,rx_id=0x%03x,"
				       "rx_dlc=%u,rx_data=%s,state=%d,rxerr=%u,txerr=%u",
				       ret, txok, req.id, req.len, data_hex,
				       rk3506_can_rx_frame.id,
				       ret == 0 ? rk3506_can_rx_frame.dlc : 0U,
				       ret == 0 ? rx_hex : "", state,
				       err_cnt.rx_err_cnt, err_cnt.tx_err_cnt);
			printk("[RK3506][CAN-RPMSG] api txrx ret=%d id=0x%03x len=%u txok=%u state=%d rxerr=%u txerr=%u\n",
			       ret, req.id, req.len, txok, state,
			       err_cnt.rx_err_cnt, err_cnt.tx_err_cnt);
		}

		if (len > 0) {
			size_t send_len = (size_t)len;

			if (send_len >= sizeof(reply)) {
				send_len = sizeof(reply) - 1U;
			}
			(void)rk3506_rpmsg_send_text(req.src, reply, send_len);
		}
	}
}
#endif /* RK3506_RPMSG_CAN_API */

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
#if defined(RK3506_RPMSG_CAN_API)
		if ((payload_len > strlen(RK3506_RPMSG_CAN_TXRX_PREFIX) &&
		     memcmp(payload, RK3506_RPMSG_CAN_TXRX_PREFIX,
			    strlen(RK3506_RPMSG_CAN_TXRX_PREFIX)) == 0) ||
		    (payload_len > strlen(RK3506_RPMSG_CAN_TX_PREFIX) &&
		     memcmp(payload, RK3506_RPMSG_CAN_TX_PREFIX,
			    strlen(RK3506_RPMSG_CAN_TX_PREFIX)) == 0) ||
		    (payload_len > strlen(RK3506_RPMSG_CAN_RX_PREFIX) &&
		     memcmp(payload, RK3506_RPMSG_CAN_RX_PREFIX,
			    strlen(RK3506_RPMSG_CAN_RX_PREFIX)) == 0)) {
			struct rk3506_can_rpmsg_req req;

			if (payload_len > strlen(RK3506_RPMSG_CAN_TXRX_PREFIX) &&
			    memcmp(payload, RK3506_RPMSG_CAN_TXRX_PREFIX,
				   strlen(RK3506_RPMSG_CAN_TXRX_PREFIX)) == 0) {
				ret = rk3506_parse_can_txrx_req(payload, payload_len,
								src, &req);
			} else if (payload_len > strlen(RK3506_RPMSG_CAN_RX_PREFIX) &&
				   memcmp(payload, RK3506_RPMSG_CAN_RX_PREFIX,
					  strlen(RK3506_RPMSG_CAN_RX_PREFIX)) == 0) {
				ret = rk3506_parse_can_rx_req(payload, payload_len,
							      src, &req);
			} else {
				ret = rk3506_parse_can_tx_req(payload, payload_len,
							      src, &req);
			}
			if (ret != 0) {
				const char bad[] = "can:err:badcmd";

				(void)rk3506_rpmsg_send_text(src, bad, strlen(bad));
				return RL_RELEASE;
			}

			ret = k_msgq_put(&rk3506_can_msgq, &req, K_NO_WAIT);
			if (ret != 0) {
				const char busy[] = "can:err:busy";

				(void)rk3506_rpmsg_send_text(src, busy, strlen(busy));
			}
			return RL_RELEASE;
		}
#endif
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
#if defined(RK3506_RPMSG_CAN_API)
	k_thread_create(&rk3506_can_thread, rk3506_can_stack,
			K_THREAD_STACK_SIZEOF(rk3506_can_stack),
			rk3506_can_worker, NULL, NULL, NULL,
			RK3506_CAN_THREAD_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&rk3506_can_thread, "rk3506_can_rpmsg");
#endif
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
