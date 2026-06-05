/*
 * SPDX-License-Identifier: MIT
 *
 * Long-run RK3506 userspace RPMsg/cache stress test.
 *
 * The test keeps one rpmsg-char endpoint open and sends one outstanding
 * ping at a time.  Each reply must be the exact matching pong.  This avoids
 * shell/process churn and stresses the real RPMsg vring/cache path for hours.
 */

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/rpmsg.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_CTRL_DEV "/dev/rpmsg_ctrl0"
#define DEFAULT_EPT_DEV "/dev/rpmsg0"
#define DEFAULT_NAME "rpmsg-ap3-ch0"
#define DEFAULT_DST 0x3003U
#define DEFAULT_SECONDS 28800ULL
#define DEFAULT_INTERVAL_US 0U
#define DEFAULT_PROGRESS_SECONDS 60U
#define DEFAULT_TIMEOUT_MS 3000

#ifndef RPMSG_ADDR_ANY
#define RPMSG_ADDR_ANY 0xffffffffU
#endif

#define RX_BUF_SIZE 512U
#define TX_BUF_SIZE 512U

#define PROBE_BASE 0x03b00000UL
#define PROBE_MAP_SIZE 0x1000UL

#define PROBE_MAGIC 0x524b5a50U
#define PROBE_EXPECTED_VERSION 14U
#define PROBE_EXPECTED_SHARED_ATTRS 0x00010019U
#define PROBE_EXPECTED_CACHE_POLICY 0x44434143U
#define PROBE_EXPECTED_THRASH_POLICY 0x54485253U

#define PROBE_OFF_MAGIC 0x00U
#define PROBE_OFF_VERSION 0x04U
#define PROBE_OFF_STATUS 0x14U
#define PROBE_OFF_IRQ_COUNT 0x20U
#define PROBE_OFF_FLAGS 0xbcU
#define PROBE_OFF_MMU_POLICY 0xd8U
#define PROBE_OFF_ATTRS_SHM 0xdcU
#define PROBE_OFF_ATTRS_RPMSG 0xe0U
#define PROBE_OFF_SCTLR 0xe4U
#define PROBE_OFF_CACHE_POLICY 0xe8U
#define PROBE_OFF_SCTLR_I 0xecU
#define PROBE_OFF_SCTLR_C 0xf0U
#define PROBE_OFF_SCTLR_A 0xf4U
#define PROBE_OFF_DCACHE_POLICY 0xf8U
#define PROBE_OFF_THRASH_POLICY 0x11cU
#define PROBE_OFF_THRASH_COUNTER 0x120U
#define PROBE_OFF_THRASH_SIGNATURE 0x124U
#define PROBE_OFF_THRASH_ERROR 0x128U

static const size_t payload_sizes[] = {
	40U, 63U, 64U, 65U,
	127U, 128U, 129U, 255U, 256U, 384U,
};

static volatile sig_atomic_t stop_requested;

struct stress_cfg {
	const char *ctrl_path;
	const char *ept_path;
	uint32_t dst;
	uint64_t seconds;
	uint32_t interval_us;
	uint32_t progress_seconds;
	int timeout_ms;
};

struct stress_stats {
	uint64_t tx;
	uint64_t rx;
	uint64_t ok;
	uint64_t fail;
	uint64_t timeout;
	uint64_t mismatch;
	uint64_t write_error;
	uint64_t read_error;
	uint64_t probe_error;
	uint64_t bytes_tx;
	uint64_t bytes_rx;
};

struct probe_state {
	uint32_t last_thrash_counter;
};

struct probe_map {
	int fd;
	volatile uint32_t *words;
};

static void on_signal(int sig)
{
	(void)sig;
	stop_requested = 1;
}

static uint64_t now_ms(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
		perror("clock_gettime");
		exit(2);
	}

	return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
}

static uint64_t parse_u64_arg(const char *arg, const char *name)
{
	char *end = NULL;
	unsigned long long val;

	errno = 0;
	val = strtoull(arg, &end, 0);
	if (errno != 0 || end == arg || *end != '\0') {
		fprintf(stderr, "invalid %s: %s\n", name, arg);
		exit(2);
	}

	return (uint64_t)val;
}

static uint32_t parse_u32_arg(const char *arg, const char *name)
{
	uint64_t val = parse_u64_arg(arg, name);

	if (val > UINT32_MAX) {
		fprintf(stderr, "%s too large: %s\n", name, arg);
		exit(2);
	}

	return (uint32_t)val;
}

static int parse_i32_arg(const char *arg, const char *name)
{
	uint64_t val = parse_u64_arg(arg, name);

	if (val > INT32_MAX) {
		fprintf(stderr, "%s too large: %s\n", name, arg);
		exit(2);
	}

	return (int)val;
}

static int wait_for_node(const char *path, unsigned int timeout_ms)
{
	struct stat st;
	unsigned int waited = 0U;

	while (stat(path, &st) != 0) {
		if (waited >= timeout_ms) {
			return -1;
		}
		usleep(10000);
		waited += 10U;
	}

	return 0;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [-s seconds] [-i interval_us] [-p progress_seconds]\n"
		"          [-t timeout_ms] [-c ctrl_dev] [-e endpoint_dev] [-d dst]\n"
		"\n"
		"Defaults: seconds=%llu interval_us=%u progress_seconds=%u\n"
		"          timeout_ms=%d ctrl=%s endpoint=%s dst=0x%x name=%s\n",
		prog, (unsigned long long)DEFAULT_SECONDS, DEFAULT_INTERVAL_US,
		DEFAULT_PROGRESS_SECONDS, DEFAULT_TIMEOUT_MS, DEFAULT_CTRL_DEV,
		DEFAULT_EPT_DEV, DEFAULT_DST, DEFAULT_NAME);
}

static void parse_args(int argc, char **argv, struct stress_cfg *cfg)
{
	int opt;

	cfg->ctrl_path = DEFAULT_CTRL_DEV;
	cfg->ept_path = DEFAULT_EPT_DEV;
	cfg->dst = DEFAULT_DST;
	cfg->seconds = DEFAULT_SECONDS;
	cfg->interval_us = DEFAULT_INTERVAL_US;
	cfg->progress_seconds = DEFAULT_PROGRESS_SECONDS;
	cfg->timeout_ms = DEFAULT_TIMEOUT_MS;

	while ((opt = getopt(argc, argv, "s:i:p:t:c:e:d:h")) != -1) {
		switch (opt) {
		case 's':
			cfg->seconds = parse_u64_arg(optarg, "seconds");
			break;
		case 'i':
			cfg->interval_us = parse_u32_arg(optarg, "interval_us");
			break;
		case 'p':
			cfg->progress_seconds = parse_u32_arg(optarg, "progress_seconds");
			break;
		case 't':
			cfg->timeout_ms = parse_i32_arg(optarg, "timeout_ms");
			break;
		case 'c':
			cfg->ctrl_path = optarg;
			break;
		case 'e':
			cfg->ept_path = optarg;
			break;
		case 'd':
			cfg->dst = parse_u32_arg(optarg, "dst");
			break;
		case 'h':
			usage(argv[0]);
			exit(0);
		default:
			usage(argv[0]);
			exit(2);
		}
	}

	if (optind != argc) {
		usage(argv[0]);
		exit(2);
	}

	if (cfg->seconds == 0ULL || cfg->progress_seconds == 0U || cfg->timeout_ms <= 0) {
		fprintf(stderr, "seconds/progress_seconds/timeout_ms must be non-zero\n");
		exit(2);
	}
}

static void probe_open(struct probe_map *probe)
{
	void *map;

	probe->fd = open("/dev/mem", O_RDONLY | O_SYNC | O_CLOEXEC);
	probe->words = NULL;
	if (probe->fd < 0) {
		fprintf(stderr, "warning: open /dev/mem failed: %s\n", strerror(errno));
		return;
	}

	map = mmap(NULL, PROBE_MAP_SIZE, PROT_READ, MAP_SHARED, probe->fd, PROBE_BASE);
	if (map == MAP_FAILED) {
		fprintf(stderr, "warning: mmap probe failed: %s\n", strerror(errno));
		close(probe->fd);
		probe->fd = -1;
		return;
	}

	probe->words = (volatile uint32_t *)map;
}

static uint32_t probe_read(const struct probe_map *probe, uint32_t off)
{
	if (probe->words == NULL || off >= PROBE_MAP_SIZE || (off & 3U) != 0U) {
		return 0xffffffffU;
	}

	return probe->words[off / 4U];
}

static int probe_expect(const struct probe_map *probe, uint32_t off,
			uint32_t expected, const char *name)
{
	uint32_t val = probe_read(probe, off);

	if (val != expected) {
		fprintf(stderr,
			"probe assert failed: %s off=%#x value=%#010x expected=%#010x\n",
			name, off, val, expected);
		return -1;
	}

	return 0;
}

static int probe_expect_nonzero(const struct probe_map *probe, uint32_t off,
				const char *name)
{
	uint32_t val = probe_read(probe, off);

	if (val == 0U || val == 0xffffffffU) {
		fprintf(stderr,
			"probe assert failed: %s off=%#x value=%#010x expected=nonzero\n",
			name, off, val);
		return -1;
	}

	return 0;
}

static int probe_assert(const struct probe_map *probe, struct stress_stats *stats,
			struct probe_state *state, int require_thrash_progress)
{
	int rc = 0;
	uint32_t thrash_counter;

	if (probe->words == NULL) {
		fprintf(stderr, "probe assert failed: /dev/mem probe is unavailable\n");
		stats->probe_error++;
		stats->fail++;
		return -1;
	}

	rc |= probe_expect(probe, PROBE_OFF_MAGIC, PROBE_MAGIC, "magic");
	rc |= probe_expect(probe, PROBE_OFF_VERSION, PROBE_EXPECTED_VERSION, "version");
	rc |= probe_expect(probe, PROBE_OFF_ATTRS_SHM, PROBE_EXPECTED_SHARED_ATTRS,
			   "amp_shmem_attrs");
	rc |= probe_expect(probe, PROBE_OFF_ATTRS_RPMSG, PROBE_EXPECTED_SHARED_ATTRS,
			   "rpmsg_attrs");
	rc |= probe_expect(probe, PROBE_OFF_SCTLR_I, 1U, "sctlr_i");
	rc |= probe_expect(probe, PROBE_OFF_SCTLR_C, 1U, "sctlr_c");
	rc |= probe_expect(probe, PROBE_OFF_SCTLR_A, 0U, "sctlr_a");
	rc |= probe_expect(probe, PROBE_OFF_DCACHE_POLICY, PROBE_EXPECTED_CACHE_POLICY,
			   "dcache_policy");
	rc |= probe_expect(probe, PROBE_OFF_THRASH_POLICY, PROBE_EXPECTED_THRASH_POLICY,
			   "thrash_policy");
	rc |= probe_expect(probe, PROBE_OFF_THRASH_ERROR, 0U, "thrash_error");
	rc |= probe_expect_nonzero(probe, PROBE_OFF_THRASH_COUNTER, "thrash_counter");
	thrash_counter = probe_read(probe, PROBE_OFF_THRASH_COUNTER);
	if (require_thrash_progress && state->last_thrash_counter == thrash_counter) {
		fprintf(stderr,
			"probe assert failed: thrash_counter stuck value=%#010x\n",
			thrash_counter);
		rc = -1;
	}
	state->last_thrash_counter = thrash_counter;

	if (rc != 0) {
		stats->probe_error++;
		stats->fail++;
		return -1;
	}

	return 0;
}

static void probe_close(struct probe_map *probe)
{
	if (probe->words != NULL) {
		munmap((void *)probe->words, PROBE_MAP_SIZE);
		probe->words = NULL;
	}
	if (probe->fd >= 0) {
		close(probe->fd);
		probe->fd = -1;
	}
}

static void print_probe(const struct probe_map *probe)
{
	printf("probe: ver=%#010x status=%#010x irq=%#010x rxirq=%#010x "
	       "rpmsg=%#010x flags=%#010x sctlr=%#010x I=%u C=%u A=%u "
	       "dcache_policy=%#010x attrs_shm=%#010x attrs_rpmsg=%#010x "
	       "thrash_policy=%#010x thrash_counter=%#010x thrash_sig=%#010x "
	       "thrash_err=%#010x\n",
	       probe_read(probe, PROBE_OFF_VERSION),
	       probe_read(probe, PROBE_OFF_STATUS),
	       probe_read(probe, PROBE_OFF_IRQ_COUNT),
	       probe_read(probe, 0x24), probe_read(probe, 0x28),
	       probe_read(probe, PROBE_OFF_FLAGS),
	       probe_read(probe, PROBE_OFF_SCTLR),
	       probe_read(probe, PROBE_OFF_SCTLR_I),
	       probe_read(probe, PROBE_OFF_SCTLR_C),
	       probe_read(probe, PROBE_OFF_SCTLR_A),
	       probe_read(probe, PROBE_OFF_DCACHE_POLICY),
	       probe_read(probe, PROBE_OFF_ATTRS_SHM),
	       probe_read(probe, PROBE_OFF_ATTRS_RPMSG),
	       probe_read(probe, PROBE_OFF_THRASH_POLICY),
	       probe_read(probe, PROBE_OFF_THRASH_COUNTER),
	       probe_read(probe, PROBE_OFF_THRASH_SIGNATURE),
	       probe_read(probe, PROBE_OFF_THRASH_ERROR));
}

static int open_endpoint(const struct stress_cfg *cfg, int *ctrl_fd, int *ept_fd)
{
	struct rpmsg_endpoint_info ept;

	*ctrl_fd = open(cfg->ctrl_path, O_RDWR | O_CLOEXEC);
	if (*ctrl_fd < 0) {
		fprintf(stderr, "open %s failed: %s\n", cfg->ctrl_path, strerror(errno));
		return -1;
	}

	memset(&ept, 0, sizeof(ept));
	strncpy(ept.name, DEFAULT_NAME, sizeof(ept.name) - 1);
	ept.src = RPMSG_ADDR_ANY;
	ept.dst = cfg->dst;

	if (ioctl(*ctrl_fd, RPMSG_CREATE_EPT_IOCTL, &ept) < 0 && errno != EEXIST) {
		fprintf(stderr, "RPMSG_CREATE_EPT_IOCTL failed: %s\n", strerror(errno));
		close(*ctrl_fd);
		return -1;
	}

	if (wait_for_node(cfg->ept_path, 5000) != 0) {
		fprintf(stderr, "endpoint not found: %s\n", cfg->ept_path);
		close(*ctrl_fd);
		return -1;
	}

	*ept_fd = open(cfg->ept_path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (*ept_fd < 0) {
		fprintf(stderr, "open %s failed: %s\n", cfg->ept_path, strerror(errno));
		close(*ctrl_fd);
		return -1;
	}

	return 0;
}

static void drain_endpoint(int ept_fd)
{
	char buf[RX_BUF_SIZE];

	for (;;) {
		ssize_t ret = read(ept_fd, buf, sizeof(buf));

		if (ret > 0) {
			continue;
		}
		if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			return;
		}
		return;
	}
}

static uint32_t crc32_update(uint32_t crc, const void *data, size_t len)
{
	const unsigned char *p = data;

	crc = ~crc;
	while (len-- != 0U) {
		crc ^= *p++;
		for (unsigned int i = 0U; i < 8U; i++) {
			crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
		}
	}

	return ~crc;
}

static int build_payload(char *tx, char *expected, size_t target_len, uint64_t seq)
{
	int hdr_len;
	uint32_t crc;
	char crc_hex[9];

	if (target_len < strlen("ping:") || target_len >= TX_BUF_SIZE) {
		return -1;
	}

	hdr_len = snprintf(tx, target_len + 1U,
			   "ping:S%016" PRIx64 ":L%03zu:C00000000:",
			   seq, target_len);
	if (hdr_len <= 0 || (size_t)hdr_len > target_len) {
		return -1;
	}

	for (size_t i = (size_t)hdr_len; i < target_len; i++) {
		tx[i] = (char)('A' + ((seq + i) % 26U));
	}
	tx[target_len] = '\0';

	crc = crc32_update(0U, tx, target_len);
	if (target_len >= (size_t)hdr_len) {
		snprintf(crc_hex, sizeof(crc_hex), "%08" PRIx32, crc);
		memcpy(tx + 29U, crc_hex, 8U);
	}

	memcpy(expected, tx, target_len + 1U);
	memcpy(expected, "pong:", strlen("pong:"));
	return 0;
}

static int run_one_ping(int ept_fd, uint64_t seq, const struct stress_cfg *cfg,
			struct stress_stats *stats)
{
	char tx[TX_BUF_SIZE];
	char expected[TX_BUF_SIZE];
	char rx[RX_BUF_SIZE];
	struct pollfd pfd;
	ssize_t ret;
	size_t tx_len = payload_sizes[(seq - 1ULL) %
				      (sizeof(payload_sizes) / sizeof(payload_sizes[0]))];

	if (build_payload(tx, expected, tx_len, seq) != 0) {
		fprintf(stderr, "payload build failed seq=%" PRIu64 " len=%zu\n",
			seq, tx_len);
		stats->fail++;
		return -1;
	}

	ret = write(ept_fd, tx, tx_len);
	if (ret != (ssize_t)tx_len) {
		fprintf(stderr, "write seq=%" PRIu64 " len=%zu failed: ret=%zd errno=%s\n",
			seq, tx_len, ret, ret < 0 ? strerror(errno) : "short write");
		stats->write_error++;
		stats->fail++;
		return -1;
	}
	stats->tx++;
	stats->bytes_tx += tx_len;

	pfd.fd = ept_fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	ret = poll(&pfd, 1, cfg->timeout_ms);
	if (ret < 0) {
		fprintf(stderr, "poll seq=%" PRIu64 " failed: %s\n",
			seq, strerror(errno));
		stats->read_error++;
		stats->fail++;
		return -1;
	}
	if (ret == 0) {
		fprintf(stderr, "timeout seq=%" PRIu64 " len=%zu after %d ms\n",
			seq, tx_len, cfg->timeout_ms);
		stats->timeout++;
		stats->fail++;
		return -1;
	}

	ret = read(ept_fd, rx, sizeof(rx) - 1U);
	if (ret < 0) {
		fprintf(stderr, "read seq=%" PRIu64 " failed: %s\n",
			seq, strerror(errno));
		stats->read_error++;
		stats->fail++;
		return -1;
	}
	rx[ret] = '\0';
	stats->rx++;
	stats->bytes_rx += (uint64_t)ret;

	if (ret != (ssize_t)tx_len || memcmp(rx, expected, tx_len) != 0) {
		fprintf(stderr,
			"mismatch seq=%" PRIu64 " tx_len=%zu rx_len=%zd tx_crc=%#010x rx_crc=%#010x\n",
			seq, tx_len, ret,
			crc32_update(0U, tx, tx_len),
			ret > 0 ? crc32_update(0U, rx, (size_t)ret) : 0U);
		stats->mismatch++;
		stats->fail++;
		return -1;
	}

	stats->ok++;
	return 0;
}

static void print_progress(const struct stress_cfg *cfg,
			   const struct stress_stats *stats,
			   const struct probe_map *probe,
			   uint64_t start_ms, uint64_t now)
{
	uint64_t elapsed_ms = now - start_ms;
	uint64_t remaining_ms = 0ULL;
	double elapsed_s = (double)elapsed_ms / 1000.0;
	double rate = elapsed_s > 0.0 ? (double)stats->ok / elapsed_s : 0.0;

	if ((cfg->seconds * 1000ULL) > elapsed_ms) {
		remaining_ms = (cfg->seconds * 1000ULL) - elapsed_ms;
	}

	printf("progress: elapsed=%" PRIu64 "s remaining=%" PRIu64 "s "
	       "tx=%" PRIu64 " rx=%" PRIu64 " ok=%" PRIu64
	       " fail=%" PRIu64 " timeout=%" PRIu64 " mismatch=%" PRIu64
	       " probe_error=%" PRIu64 " bytes_tx=%" PRIu64 " bytes_rx=%" PRIu64
	       " rate=%.1f/s\n",
	       (uint64_t)(elapsed_ms / 1000ULL), (uint64_t)(remaining_ms / 1000ULL),
	       stats->tx, stats->rx, stats->ok, stats->fail,
	       stats->timeout, stats->mismatch, stats->probe_error,
	       stats->bytes_tx, stats->bytes_rx, rate);
	print_probe(probe);
	fflush(stdout);
}

int main(int argc, char **argv)
{
	struct stress_cfg cfg;
	struct stress_stats stats;
	struct probe_state probe_state = { .last_thrash_counter = 0U };
	struct probe_map probe = { .fd = -1, .words = NULL };
	uint64_t start_ms;
	uint64_t deadline_ms;
	uint64_t next_progress_ms;
	uint64_t seq = 1ULL;
	int ctrl_fd = -1;
	int ept_fd = -1;
	int rc = 1;

	setvbuf(stdout, NULL, _IOLBF, 0);
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

	parse_args(argc, argv, &cfg);
	memset(&stats, 0, sizeof(stats));
	probe_open(&probe);

	if (open_endpoint(&cfg, &ctrl_fd, &ept_fd) != 0) {
		goto out;
	}
	drain_endpoint(ept_fd);
	if (probe_assert(&probe, &stats, &probe_state, 0) != 0) {
		print_probe(&probe);
		goto out;
	}

	start_ms = now_ms();
	deadline_ms = start_ms + (cfg.seconds * 1000ULL);
	next_progress_ms = start_ms + ((uint64_t)cfg.progress_seconds * 1000ULL);

	printf("rk3506 cache stress start: seconds=%" PRIu64
	       " interval_us=%u timeout_ms=%d ctrl=%s endpoint=%s name=%s dst=0x%x\n",
	       cfg.seconds, cfg.interval_us, cfg.timeout_ms, cfg.ctrl_path,
	       cfg.ept_path, DEFAULT_NAME, cfg.dst);
	print_probe(&probe);

	while (!stop_requested && now_ms() < deadline_ms) {
		uint64_t now;

		if (run_one_ping(ept_fd, seq, &cfg, &stats) != 0) {
			print_progress(&cfg, &stats, &probe, start_ms, now_ms());
			goto out;
		}
		seq++;

		if (cfg.interval_us != 0U) {
			usleep(cfg.interval_us);
		}

		now = now_ms();
		if (now >= next_progress_ms) {
			if (probe_assert(&probe, &stats, &probe_state, 1) != 0) {
				print_progress(&cfg, &stats, &probe, start_ms, now);
				goto out;
			}
			print_progress(&cfg, &stats, &probe, start_ms, now);
			next_progress_ms = now + ((uint64_t)cfg.progress_seconds * 1000ULL);
		}
	}

	print_progress(&cfg, &stats, &probe, start_ms, now_ms());
	if (stop_requested) {
		fprintf(stderr, "stopped by signal\n");
		rc = 130;
	} else if (stats.fail == 0ULL && stats.tx == stats.rx && stats.rx == stats.ok) {
		printf("RESULT: PASS tx=%" PRIu64 " rx=%" PRIu64 " ok=%" PRIu64 "\n",
		       stats.tx, stats.rx, stats.ok);
		rc = 0;
	} else {
		fprintf(stderr, "RESULT: FAIL tx=%" PRIu64 " rx=%" PRIu64
			" ok=%" PRIu64 " fail=%" PRIu64 "\n",
			stats.tx, stats.rx, stats.ok, stats.fail);
		rc = 1;
	}

out:
	if (ept_fd >= 0) {
		close(ept_fd);
	}
	if (ctrl_fd >= 0) {
		close(ctrl_fd);
	}
	probe_close(&probe);
	return rc;
}
