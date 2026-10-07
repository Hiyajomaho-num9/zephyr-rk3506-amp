// SPDX-License-Identifier: MIT
/*
 * Minimal SocketCAN sender for RK3506 CAN0 golden-baseline testing.
 *
 * Usage:
 *   rk3506_socketcan_send can0 0x11b 0101030301000000 100 1000
 */

#include <errno.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static int parse_u32(const char *s, uint32_t *out)
{
	char *end = NULL;
	unsigned long v;

	errno = 0;
	v = strtoul(s, &end, 0);
	if (errno || end == s || *end != '\0' || v > UINT32_MAX) {
		return -1;
	}

	*out = (uint32_t)v;
	return 0;
}

static int hex_nibble(char c)
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
	return -1;
}

static int parse_hex_data(const char *hex, uint8_t *data, uint8_t *len)
{
	size_t n = strlen(hex);
	size_t i;

	if ((n & 1U) != 0U || n > 16U) {
		return -1;
	}

	for (i = 0; i < n / 2U; i++) {
		int hi = hex_nibble(hex[i * 2U]);
		int lo = hex_nibble(hex[i * 2U + 1U]);

		if (hi < 0 || lo < 0) {
			return -1;
		}
		data[i] = (uint8_t)((hi << 4) | lo);
	}

	*len = (uint8_t)(n / 2U);
	return 0;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s <ifname> <std-id> <hex-data <=8B> [repeat] [gap_us]\n"
		"Example: %s can0 0x11b 0101030301000000 100 1000\n",
		prog, prog);
}

int main(int argc, char **argv)
{
	struct sockaddr_can addr;
	struct ifreq ifr;
	struct can_frame frame;
	const char *ifname;
	uint32_t id;
	uint32_t repeat = 1;
	uint32_t gap_us = 0;
	int fd;
	uint32_t i;

	if (argc < 4 || argc > 6) {
		usage(argv[0]);
		return 2;
	}

	ifname = argv[1];
	if (parse_u32(argv[2], &id) != 0 || id > CAN_SFF_MASK) {
		fprintf(stderr, "invalid standard CAN id: %s\n", argv[2]);
		return 2;
	}

	memset(&frame, 0, sizeof(frame));
	frame.can_id = id;
	if (parse_hex_data(argv[3], frame.data, &frame.can_dlc) != 0) {
		fprintf(stderr, "invalid hex data, expected even-length <=16 hex chars\n");
		return 2;
	}

	if (argc >= 5 && (parse_u32(argv[4], &repeat) != 0 || repeat == 0U)) {
		fprintf(stderr, "invalid repeat: %s\n", argv[4]);
		return 2;
	}
	if (argc >= 6 && parse_u32(argv[5], &gap_us) != 0) {
		fprintf(stderr, "invalid gap_us: %s\n", argv[5]);
		return 2;
	}

	fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (fd < 0) {
		perror("socket(PF_CAN)");
		return 1;
	}

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1U);
	if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
		perror("ioctl(SIOCGIFINDEX)");
		close(fd);
		return 1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.can_family = AF_CAN;
	addr.can_ifindex = ifr.ifr_ifindex;
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind(AF_CAN)");
		close(fd);
		return 1;
	}

	for (i = 0; i < repeat; i++) {
		ssize_t n = write(fd, &frame, sizeof(frame));

		if (n != (ssize_t)sizeof(frame)) {
			if (n < 0) {
				perror("write(CAN frame)");
			} else {
				fprintf(stderr, "short write: %zd/%zu\n", n, sizeof(frame));
			}
			close(fd);
			return 1;
		}

		if (gap_us != 0U) {
			usleep(gap_us);
		}
	}

	printf("sent=%u if=%s id=0x%x dlc=%u data=%s gap_us=%u\n",
	       repeat, ifname, id, frame.can_dlc, argv[3], gap_us);
	close(fd);
	return 0;
}
