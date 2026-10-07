/*
 * SPDX-License-Identifier: MIT
 *
 * Linux userspace TX-only diagnostic tool for RK3506 RPMsg -> Zephyr CAN0.
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
#define DEFAULT_REPEAT 1U
#define DEFAULT_GAP_US 0U
#define DEFAULT_RPMSG_TIMEOUT_MS 5000
#define RX_BUF_SIZE 512U
#define TX_BUF_SIZE 160U

#ifndef RPMSG_ADDR_ANY
#define RPMSG_ADDR_ANY 0xffffffffU
#endif

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
		"Usage: %s [id] [hex_data] [repeat] [gap_us] [ctrl_dev] [dst] [endpoint_dev]\n"
		"Default: %s %s %s %u %u %s 0x%x %s\n"
		"Example full-1 waveform: %s 0x1b ffffffffffffffff 1000 1000\n"
		"Example STM32G4 frame:   %s 0x1b 0101030301000000 1000 1000\n",
		prog, prog, DEFAULT_ID, DEFAULT_DATA, DEFAULT_REPEAT, DEFAULT_GAP_US,
		DEFAULT_CTRL_DEV, DEFAULT_DST, DEFAULT_EPT_DEV, prog, prog);
}

int main(int argc, char **argv)
{
	const char *id_s = DEFAULT_ID;
	const char *data_s = DEFAULT_DATA;
	const char *ctrl_path = DEFAULT_CTRL_DEV;
	const char *ept_path = DEFAULT_EPT_DEV;
	unsigned int repeat = DEFAULT_REPEAT;
	unsigned int gap_us = DEFAULT_GAP_US;
	unsigned int dst = DEFAULT_DST;
	unsigned int id;
	struct rpmsg_endpoint_info ept;
	struct pollfd pfd;
	char tx[TX_BUF_SIZE];
	char rx[RX_BUF_SIZE];
	ssize_t ret;
	int ctrl_fd;
	int ept_fd;

	if (argc > 8) {
		usage(argv[0]);
		return 2;
	}

	if (argc >= 2) {
		id_s = argv[1];
	}
	if (argc >= 3) {
		data_s = argv[2];
	}
	if (argc >= 4) {
		repeat = parse_u32(argv[3], "repeat");
	}
	if (argc >= 5) {
		gap_us = parse_u32(argv[4], "gap_us");
	}
	if (argc >= 6) {
		ctrl_path = argv[5];
	}
	if (argc >= 7) {
		dst = parse_u32(argv[6], "dst");
	}
	if (argc >= 8) {
		ept_path = argv[7];
	}

	id = parse_u32(id_s, "id");
	if (id > 0x7ffU) {
		fprintf(stderr, "only standard 11-bit CAN id is supported: 0x%x\n", id);
		return 2;
	}
	if (!is_hex_payload(data_s)) {
		fprintf(stderr, "hex_data must be even-length hex, max 8 bytes: %s\n", data_s);
		return 2;
	}
	if (repeat == 0U || repeat > 100000U) {
		fprintf(stderr, "repeat must be 1..100000\n");
		return 2;
	}
	if (gap_us > 1000000U) {
		fprintf(stderr, "gap_us must be <= 1000000\n");
		return 2;
	}

	ret = snprintf(tx, sizeof(tx), "can:tx:0x%x:%s:%u:%u",
		       id, data_s, repeat, gap_us);
	if (ret < 0 || (size_t)ret >= sizeof(tx)) {
		fprintf(stderr, "tx command too large\n");
		return 2;
	}

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
	ret = poll(&pfd, 1, DEFAULT_RPMSG_TIMEOUT_MS);
	if (ret < 0) {
		fprintf(stderr, "poll failed: %s\n", strerror(errno));
		close(ept_fd);
		close(ctrl_fd);
		return 1;
	}
	if (ret == 0) {
		fprintf(stderr, "timeout waiting for rpmsg CAN TX response\n");
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
	return strncmp(rx, "can:tx:", strlen("can:tx:")) == 0 ? 0 : 1;
}
