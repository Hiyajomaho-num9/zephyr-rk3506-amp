/*
 * SPDX-License-Identifier: MIT
 *
 * Minimal RK3506 userspace RPMsg char ping tool.
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

#ifndef RPMSG_ADDR_ANY
#define RPMSG_ADDR_ANY 0xffffffffU
#endif

#define DEFAULT_CTRL_DEV "/dev/rpmsg_ctrl0"
#define DEFAULT_EPT_DEV "/dev/rpmsg0"
#define DEFAULT_NAME "rpmsg-ap3-ch0"
#define DEFAULT_DST 0x3003U
#define DEFAULT_TX "ping:1"
#define RX_BUF_SIZE 512U

static unsigned int parse_u32(const char *s)
{
	char *end = NULL;
	unsigned long val;

	errno = 0;
	val = strtoul(s, &end, 0);
	if (errno != 0 || end == s || *end != '\0' || val > UINT32_MAX) {
		fprintf(stderr, "invalid u32: %s\n", s);
		exit(2);
	}

	return (unsigned int)val;
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
		"Usage: %s [ctrl_dev] [tx] [dst] [endpoint_dev]\n"
		"Default: %s %s %s 0x%x %s\n",
		prog, prog, DEFAULT_CTRL_DEV, DEFAULT_TX, DEFAULT_DST,
		DEFAULT_EPT_DEV);
}

int main(int argc, char **argv)
{
	const char *ctrl_path = DEFAULT_CTRL_DEV;
	const char *tx = DEFAULT_TX;
	const char *ept_path = DEFAULT_EPT_DEV;
	unsigned int dst = DEFAULT_DST;
	struct rpmsg_endpoint_info ept;
	struct pollfd pfd;
	char rx[RX_BUF_SIZE];
	ssize_t ret;
	int ctrl_fd;
	int ept_fd;

	if (argc > 5) {
		usage(argv[0]);
		return 2;
	}

	if (argc >= 2) {
		ctrl_path = argv[1];
	}
	if (argc >= 3) {
		tx = argv[2];
	}
	if (argc >= 4) {
		dst = parse_u32(argv[3]);
	}
	if (argc >= 5) {
		ept_path = argv[4];
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
	ret = poll(&pfd, 1, 3000);
	if (ret < 0) {
		fprintf(stderr, "poll failed: %s\n", strerror(errno));
		close(ept_fd);
		close(ctrl_fd);
		return 1;
	}
	if (ret == 0) {
		fprintf(stderr, "timeout waiting for rpmsg response\n");
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
	return 0;
}
