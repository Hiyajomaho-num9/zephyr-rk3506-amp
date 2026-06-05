/*
 * SPDX-License-Identifier: MIT
 *
 * Linux userspace RPMsg bridge tool for RK3506 Zephyr CAN.
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/rpmsg.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_CTRL_DEV "/dev/rpmsg_ctrl0"
#define DEFAULT_EPT_DEV "/dev/rpmsg0"
#define DEFAULT_NAME "rpmsg-ap3-ch0"
#define DEFAULT_DST 0x3003U
#define DEFAULT_ID "0x1b"
#define DEFAULT_DATA "0101030301000000"
#define DEFAULT_CAN_TIMEOUT_MS 1000U
#define DEFAULT_RPMSG_TIMEOUT_MS 5000U
#define DEFAULT_REPEAT 1U
#define DEFAULT_GAP_US 0U
#define RX_BUF_SIZE 512U
#define TX_BUF_SIZE 160U

#ifndef RPMSG_ADDR_ANY
#define RPMSG_ADDR_ANY 0xffffffffU
#endif

enum can_cmd_mode {
	CAN_CMD_TX,
	CAN_CMD_RX,
	CAN_CMD_TXRX,
};

static unsigned int parse_u32(const char *s, const char *name)
{
	char *end = NULL;
	unsigned long val;

	errno = 0;
	val = strtoul(s, &end, 0);
	if (errno != 0 || end == s || *end != '\0' || val > UINT32_MAX) {
		fprintf(stderr, "invalid %s: %s\n", name, s);
		exit(2);
	}

	return (unsigned int)val;
}

static int is_hex_payload(const char *s)
{
	size_t len = strlen(s);

	if ((len % 2U) != 0U || len > 16U) {
		return 0;
	}

	for (size_t i = 0U; i < len; i++) {
		if (!((s[i] >= '0' && s[i] <= '9') ||
		      (s[i] >= 'a' && s[i] <= 'f') ||
		      (s[i] >= 'A' && s[i] <= 'F'))) {
			return 0;
		}
	}

	return 1;
}

static int wait_for_node(const char *path, unsigned int timeout_ms)
{
	struct stat st;
	unsigned int waited = 0;

	while (stat(path, &st) != 0) {
		if (waited >= timeout_ms) {
			return -1;
		}
		usleep(10000);
		waited += 10;
	}

	return 0;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage:\n"
		"  %s tx   [id] [hex_data] [repeat] [gap_us] [rpmsg_timeout_ms] [ctrl_dev] [dst] [endpoint_dev]\n"
		"  %s rx   [timeout_ms] [rpmsg_timeout_ms] [ctrl_dev] [dst] [endpoint_dev]\n"
		"  %s rx   any [timeout_ms] [rpmsg_timeout_ms] [ctrl_dev] [dst] [endpoint_dev]\n"
		"  %s rx   <id> <mask> [timeout_ms] [rpmsg_timeout_ms] [ctrl_dev] [dst] [endpoint_dev]\n"
		"  %s txrx [id] [hex_data] [can_timeout_ms] [rpmsg_timeout_ms] [ctrl_dev] [dst] [endpoint_dev]\n"
		"Legacy TX-once form is still accepted:\n"
		"  %s [id] [hex_data] [rpmsg_timeout_ms] [ctrl_dev] [dst] [endpoint_dev]\n"
		"Examples:\n"
		"  %s tx 0x1b 0101030301000000\n"
		"  %s tx 0x1b ffffffffffffffff 1000 1000\n"
		"  %s rx any 10000\n"
		"  %s rx 0x1b 0x7ff 10000\n"
		"  %s txrx 0x1b 0101030301000000 1000\n",
		prog, prog, prog, prog, prog, prog,
		prog, prog, prog, prog, prog);
}

static void validate_can_id(unsigned int id)
{
	if (id > 0x7ffU) {
		fprintf(stderr, "only standard 11-bit CAN id is supported: 0x%x\n", id);
		exit(2);
	}
}

static void validate_timeout(const char *name, unsigned int timeout_ms)
{
	if (timeout_ms == 0U || timeout_ms > 600000U) {
		fprintf(stderr, "%s must be 1..600000\n", name);
		exit(2);
	}
}

static int build_legacy_tx(int argc, char **argv, char *tx, size_t tx_size,
			   unsigned int *rpmsg_timeout_ms, const char **ctrl_path,
			   unsigned int *dst, const char **ept_path)
{
	const char *id_s = DEFAULT_ID;
	const char *data_s = DEFAULT_DATA;
	unsigned int id;
	int ret;

	if (argc > 7) {
		return -EINVAL;
	}
	if (argc >= 2) {
		id_s = argv[1];
	}
	if (argc >= 3) {
		data_s = argv[2];
	}
	if (argc >= 4) {
		*rpmsg_timeout_ms = parse_u32(argv[3], "rpmsg_timeout_ms");
	}
	if (argc >= 5) {
		*ctrl_path = argv[4];
	}
	if (argc >= 6) {
		*dst = parse_u32(argv[5], "dst");
	}
	if (argc >= 7) {
		*ept_path = argv[6];
	}

	id = parse_u32(id_s, "id");
	validate_can_id(id);
	if (!is_hex_payload(data_s)) {
		fprintf(stderr, "hex_data must be even-length hex, max 8 bytes: %s\n", data_s);
		return -EINVAL;
	}

	ret = snprintf(tx, tx_size, "can:tx:0x%x:%s:1:0", id, data_s);
	return (ret < 0 || (size_t)ret >= tx_size) ? -EINVAL : 0;
}

static int build_tx_cmd(int argc, char **argv, char *tx, size_t tx_size,
			unsigned int *rpmsg_timeout_ms, const char **ctrl_path,
			unsigned int *dst, const char **ept_path)
{
	const char *id_s = DEFAULT_ID;
	const char *data_s = DEFAULT_DATA;
	unsigned int repeat = DEFAULT_REPEAT;
	unsigned int gap_us = DEFAULT_GAP_US;
	unsigned int id;
	int ret;

	if (argc > 10) {
		return -EINVAL;
	}
	if (argc >= 3) {
		id_s = argv[2];
	}
	if (argc >= 4) {
		data_s = argv[3];
	}
	if (argc >= 5) {
		repeat = parse_u32(argv[4], "repeat");
	}
	if (argc >= 6) {
		gap_us = parse_u32(argv[5], "gap_us");
	}
	if (argc >= 7) {
		*rpmsg_timeout_ms = parse_u32(argv[6], "rpmsg_timeout_ms");
	}
	if (argc >= 8) {
		*ctrl_path = argv[7];
	}
	if (argc >= 9) {
		*dst = parse_u32(argv[8], "dst");
	}
	if (argc >= 10) {
		*ept_path = argv[9];
	}

	id = parse_u32(id_s, "id");
	validate_can_id(id);
	if (!is_hex_payload(data_s)) {
		fprintf(stderr, "hex_data must be even-length hex, max 8 bytes: %s\n", data_s);
		return -EINVAL;
	}
	if (repeat == 0U || repeat > 100000U) {
		fprintf(stderr, "repeat must be 1..100000\n");
		return -EINVAL;
	}
	if (gap_us > 1000000U) {
		fprintf(stderr, "gap_us must be <= 1000000\n");
		return -EINVAL;
	}

	ret = snprintf(tx, tx_size, "can:tx:0x%x:%s:%u:%u",
		       id, data_s, repeat, gap_us);
	return (ret < 0 || (size_t)ret >= tx_size) ? -EINVAL : 0;
}

static int build_txrx_cmd(int argc, char **argv, char *tx, size_t tx_size,
			  unsigned int *rpmsg_timeout_ms, const char **ctrl_path,
			  unsigned int *dst, const char **ept_path)
{
	const char *id_s = DEFAULT_ID;
	const char *data_s = DEFAULT_DATA;
	unsigned int can_timeout_ms = DEFAULT_CAN_TIMEOUT_MS;
	unsigned int id;
	int ret;

	if (argc > 9) {
		return -EINVAL;
	}
	if (argc >= 3) {
		id_s = argv[2];
	}
	if (argc >= 4) {
		data_s = argv[3];
	}
	if (argc >= 5) {
		can_timeout_ms = parse_u32(argv[4], "can_timeout_ms");
	}
	if (argc >= 6) {
		*rpmsg_timeout_ms = parse_u32(argv[5], "rpmsg_timeout_ms");
	}
	if (argc >= 7) {
		*ctrl_path = argv[6];
	}
	if (argc >= 8) {
		*dst = parse_u32(argv[7], "dst");
	}
	if (argc >= 9) {
		*ept_path = argv[8];
	}

	id = parse_u32(id_s, "id");
	validate_can_id(id);
	validate_timeout("can_timeout_ms", can_timeout_ms);
	if (*rpmsg_timeout_ms <= can_timeout_ms && can_timeout_ms < 600000U) {
		*rpmsg_timeout_ms = can_timeout_ms + 1000U;
	}
	if (!is_hex_payload(data_s)) {
		fprintf(stderr, "hex_data must be even-length hex, max 8 bytes: %s\n", data_s);
		return -EINVAL;
	}

	ret = snprintf(tx, tx_size, "can:txrx:0x%x:%s:%u",
		       id, data_s, can_timeout_ms);
	return (ret < 0 || (size_t)ret >= tx_size) ? -EINVAL : 0;
}

static int build_rx_cmd(int argc, char **argv, char *tx, size_t tx_size,
			unsigned int *rpmsg_timeout_ms, const char **ctrl_path,
			unsigned int *dst, const char **ept_path)
{
	unsigned int can_timeout_ms = DEFAULT_CAN_TIMEOUT_MS;
	unsigned int id = 0U;
	unsigned int mask = 0U;
	int ret;

	if (argc > 9) {
		return -EINVAL;
	}

	if (argc == 2) {
		ret = snprintf(tx, tx_size, "can:rx:%u", can_timeout_ms);
		return (ret < 0 || (size_t)ret >= tx_size) ? -EINVAL : 0;
	}

	if (strcmp(argv[2], "any") == 0) {
		if (argc > 8) {
			return -EINVAL;
		}
		if (argc >= 4) {
			can_timeout_ms = parse_u32(argv[3], "can_timeout_ms");
		}
		if (argc >= 5) {
			*rpmsg_timeout_ms = parse_u32(argv[4], "rpmsg_timeout_ms");
		}
		if (argc >= 6) {
			*ctrl_path = argv[5];
		}
		if (argc >= 7) {
			*dst = parse_u32(argv[6], "dst");
		}
		if (argc >= 8) {
			*ept_path = argv[7];
		}
		validate_timeout("can_timeout_ms", can_timeout_ms);
		if (*rpmsg_timeout_ms <= can_timeout_ms && can_timeout_ms < 600000U) {
			*rpmsg_timeout_ms = can_timeout_ms + 1000U;
		}
		ret = snprintf(tx, tx_size, "can:rx:any:%u", can_timeout_ms);
		return (ret < 0 || (size_t)ret >= tx_size) ? -EINVAL : 0;
	}

	if (argc == 3) {
		can_timeout_ms = parse_u32(argv[2], "can_timeout_ms");
		validate_timeout("can_timeout_ms", can_timeout_ms);
		if (*rpmsg_timeout_ms <= can_timeout_ms && can_timeout_ms < 600000U) {
			*rpmsg_timeout_ms = can_timeout_ms + 1000U;
		}
		ret = snprintf(tx, tx_size, "can:rx:%u", can_timeout_ms);
		return (ret < 0 || (size_t)ret >= tx_size) ? -EINVAL : 0;
	}

	id = parse_u32(argv[2], "id");
	mask = parse_u32(argv[3], "mask");
	validate_can_id(id);
	validate_can_id(mask);

	if (argc >= 5) {
		can_timeout_ms = parse_u32(argv[4], "can_timeout_ms");
	}
	if (argc >= 6) {
		*rpmsg_timeout_ms = parse_u32(argv[5], "rpmsg_timeout_ms");
	}
	if (argc >= 7) {
		*ctrl_path = argv[6];
	}
	if (argc >= 8) {
		*dst = parse_u32(argv[7], "dst");
	}
	if (argc >= 9) {
		*ept_path = argv[8];
	}
	validate_timeout("can_timeout_ms", can_timeout_ms);
	if (*rpmsg_timeout_ms <= can_timeout_ms && can_timeout_ms < 600000U) {
		*rpmsg_timeout_ms = can_timeout_ms + 1000U;
	}

	ret = snprintf(tx, tx_size, "can:rx:0x%x:0x%x:%u", id, mask, can_timeout_ms);
	return (ret < 0 || (size_t)ret >= tx_size) ? -EINVAL : 0;
}

static int response_status(const char *rx)
{
	const char *ret;

	if (strncmp(rx, "can:", strlen("can:")) != 0) {
		return 1;
	}

	ret = strstr(rx, "ret=");
	if (ret == NULL) {
		return 1;
	}

	ret += strlen("ret=");
	return (ret[0] == '0' && (ret[1] == ',' || ret[1] == '\0')) ? 0 : 1;
}

int main(int argc, char **argv)
{
	const char *ctrl_path = DEFAULT_CTRL_DEV;
	const char *ept_path = DEFAULT_EPT_DEV;
	unsigned int rpmsg_timeout_ms = DEFAULT_RPMSG_TIMEOUT_MS;
	unsigned int dst = DEFAULT_DST;
	struct rpmsg_endpoint_info ept;
	struct pollfd pfd;
	char tx[TX_BUF_SIZE];
	char rx[RX_BUF_SIZE];
	ssize_t ret;
	int ctrl_fd;
	int ept_fd;
	int build_ret;

	if (argc >= 2 && strcmp(argv[1], "-h") == 0) {
		usage(argv[0]);
		return 0;
	}

	if (argc >= 2 && strcmp(argv[1], "tx") == 0) {
		build_ret = build_tx_cmd(argc, argv, tx, sizeof(tx),
					 &rpmsg_timeout_ms, &ctrl_path, &dst,
					 &ept_path);
	} else if (argc >= 2 && strcmp(argv[1], "rx") == 0) {
		build_ret = build_rx_cmd(argc, argv, tx, sizeof(tx),
					 &rpmsg_timeout_ms, &ctrl_path, &dst,
					 &ept_path);
	} else if (argc >= 2 && strcmp(argv[1], "txrx") == 0) {
		build_ret = build_txrx_cmd(argc, argv, tx, sizeof(tx),
					   &rpmsg_timeout_ms, &ctrl_path, &dst,
					   &ept_path);
	} else {
		build_ret = build_legacy_tx(argc, argv, tx, sizeof(tx),
					    &rpmsg_timeout_ms, &ctrl_path, &dst,
					    &ept_path);
	}

	if (build_ret != 0) {
		usage(argv[0]);
		return 2;
	}
	validate_timeout("rpmsg_timeout_ms", rpmsg_timeout_ms);

	ctrl_fd = open(ctrl_path, O_RDWR | O_CLOEXEC);
	if (ctrl_fd < 0) {
		fprintf(stderr, "open %s failed: %s\n", ctrl_path, strerror(errno));
		return 1;
	}

	memset(&ept, 0, sizeof(ept));
	strncpy(ept.name, DEFAULT_NAME, sizeof(ept.name) - 1);
	ept.src = RPMSG_ADDR_ANY;
	ept.dst = dst;

	if (ioctl(ctrl_fd, RPMSG_CREATE_EPT_IOCTL, &ept) < 0 && errno != EEXIST) {
		fprintf(stderr, "RPMSG_CREATE_EPT_IOCTL failed: %s\n", strerror(errno));
		close(ctrl_fd);
		return 1;
	}

	if (wait_for_node(ept_path, 3000) != 0) {
		fprintf(stderr, "endpoint not found: %s\n", ept_path);
		close(ctrl_fd);
		return 1;
	}

	ept_fd = open(ept_path, O_RDWR | O_CLOEXEC);
	if (ept_fd < 0) {
		fprintf(stderr, "open %s failed: %s\n", ept_path, strerror(errno));
		close(ctrl_fd);
		return 1;
	}

	printf("ctrl=%s endpoint=%s name=%s dst=0x%x tx=\"%s\"\n",
	       ctrl_path, ept_path, DEFAULT_NAME, dst, tx);

	ret = write(ept_fd, tx, strlen(tx));
	if (ret < 0) {
		fprintf(stderr, "write failed: %s\n", strerror(errno));
		close(ept_fd);
		close(ctrl_fd);
		return 1;
	}

	pfd.fd = ept_fd;
	pfd.events = POLLIN;
	ret = poll(&pfd, 1, (int)rpmsg_timeout_ms);
	if (ret < 0) {
		fprintf(stderr, "poll failed: %s\n", strerror(errno));
		close(ept_fd);
		close(ctrl_fd);
		return 1;
	}
	if (ret == 0) {
		fprintf(stderr, "timeout waiting for rpmsg CAN response\n");
		close(ept_fd);
		close(ctrl_fd);
		return 1;
	}

	ret = read(ept_fd, rx, sizeof(rx) - 1);
	if (ret < 0) {
		fprintf(stderr, "read failed: %s\n", strerror(errno));
		close(ept_fd);
		close(ctrl_fd);
		return 1;
	}

	rx[ret] = '\0';
	printf("rx len=%zd data=\"%s\"\n", ret, rx);

	close(ept_fd);
	close(ctrl_fd);
	return response_status(rx);
}
