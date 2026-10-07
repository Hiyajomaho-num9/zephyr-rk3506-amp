/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: Apache-2.0
 *
 * RK3506 SPI0/SPI1 board test for the CPU2 (AMP) image.
 *
 * 1. Self-test: SPI modes 0-3, 8/16-bit frames, MSB/LSB first, lengths
 *    around the 32-frame FIFO window, split/asymmetric/NULL buffers, rate
 *    steps from 12 MHz down to 367 Hz (timed), 4 KiB transfers, argument
 *    checks and SPI_LOCK_ON. Runs at boot and again at uptime 30 s, after
 *    Linux has booted.
 * 2. Soak: one 256-byte transfer per controller every second, forever.
 * 3. Clock watch: the CRU gates of both controllers, plus CAN0 as a canary,
 *    polled every 10 ms. Linux gates enabled clocks it does not own in
 *    clk_disable_unused(); that shows up here as an EVENT line.
 *
 * A controller uses its internal loopback and no pads, unless its node has a
 * default pinctrl state: then transfers go over the pads and MOSI must be
 * jumpered to MISO (see external-spi1.overlay).
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(spi_test, LOG_LEVEL_INF);

#define INPUT_HZ	24000000U
#define LONG_LEN	4096U
#define SOAK_LEN	256U
#define SOAK_PERIOD_MS	1000
#define GUARD		16U
#define MONITOR_MS	10
#define STALL_MS	5000
#define SELFTEST2_AT_MS	30000
/* Status line every second until then, every 10 s afterwards. */
#define FAST_REPORT_MS	60000
/* Detailed soak failure logs per controller. */
#define SOAK_ERR_LOGS	10U

#define ZEPHYR_USER DT_PATH(zephyr_user)

struct bus {
	const char *name;
	const struct device *dev;
	/* Over the pads with MOSI jumpered to MISO, instead of internal loopback. */
	bool external;
	/* Native chip select line; an overlay sets zephyr,user spiN-cs to change it. */
	uint16_t cs;
	uint32_t ok;
	uint32_t fail;
};

#define BUS(label) { \
	.name = #label, \
	.dev = DEVICE_DT_GET(DT_NODELABEL(label)), \
	.external = DT_PINCTRL_HAS_NAME(DT_NODELABEL(label), default), \
	.cs = DT_PROP_OR(ZEPHYR_USER, label##_cs, 0), \
}

static struct bus buses[] = {BUS(spi0), BUS(spi1)};

struct watch {
	const char *name;
	const struct device *dev;
	clock_control_subsys_t sys;
	enum clock_control_status last;
	/* Transitions into OFF. */
	uint32_t offs;
};

#define WATCH(label, clk, text) { \
	.name = text, \
	.dev = DEVICE_DT_GET(DT_CLOCKS_CTLR_BY_NAME(DT_NODELABEL(label), clk)), \
	.sys = (clock_control_subsys_t)DT_CLOCKS_CELL_BY_NAME(DT_NODELABEL(label), clk, id), \
}

/* Kept in pclk/clk pairs: the status line prints them in this order. */
static struct watch watches[] = {
	WATCH(spi0, apb_pclk, "spi0.pclk"),
	WATCH(spi0, spiclk, "spi0.clk"),
	WATCH(spi1, apb_pclk, "spi1.pclk"),
	WATCH(spi1, spiclk, "spi1.clk"),
	/*
	 * Canary: in the pingpong image CAN0 belongs to CPU2 and its driver
	 * ungates these at boot. Here nothing turns them back on, so if Linux
	 * gates them they stay off.
	 */
	WATCH(can0, apb_pclk, "can0.hclk"),
	WATCH(can0, baudclk, "can0.clk"),
};
#define CANARY_FIRST 4U

static K_MUTEX_DEFINE(watch_lock);
static K_THREAD_STACK_DEFINE(monitor_stack, 4096);
static struct k_thread monitor_thread;

/* Main thread progress, for the stall report. */
static atomic_t progress;
static const char *volatile step_bus = "-";
static const char *volatile step = "start";

static struct {
	uint32_t cases;
	uint32_t fails;
} tally;

static uint8_t tx_buf[LONG_LEN];
static uint8_t rx_buf[LONG_LEN + GUARD];
static uint8_t exp_buf[LONG_LEN];
static uint32_t rng = 0x2545f491U;

static const char *status_name(enum clock_control_status status)
{
	switch (status) {
	case CLOCK_CONTROL_STATUS_ON:
		return "on";
	case CLOCK_CONTROL_STATUS_OFF:
		return "off";
	default:
		return "?";
	}
}

static void watch_poll(const char *who)
{
	k_mutex_lock(&watch_lock, K_FOREVER);
	ARRAY_FOR_EACH_PTR(watches, w) {
		enum clock_control_status now = clock_control_get_status(w->dev, w->sys);

		if (now == w->last) {
			continue;
		}
		if (now == CLOCK_CONTROL_STATUS_OFF) {
			w->offs++;
		}
		LOG_WRN("EVENT t=%u ms %s %s -> %s (seen by %s)", (uint32_t)k_uptime_get(),
			w->name, status_name(w->last), status_name(now), who);
		w->last = now;
	}
	k_mutex_unlock(&watch_lock);
}

static void watch_init(void)
{
	for (size_t i = CANARY_FIRST; i < ARRAY_SIZE(watches); i++) {
		int ret = clock_control_on(watches[i].dev, watches[i].sys);

		if (ret != 0) {
			LOG_WRN("canary %s: clock_control_on() = %d", watches[i].name, ret);
		}
	}
	ARRAY_FOR_EACH_PTR(watches, w) {
		w->last = clock_control_get_status(w->dev, w->sys);
	}
	LOG_INF("clocks (pclk/clk): spi0 %s/%s, spi1 %s/%s, can0 canary %s/%s",
		status_name(watches[0].last), status_name(watches[1].last),
		status_name(watches[2].last), status_name(watches[3].last),
		status_name(watches[4].last), status_name(watches[5].last));
}

static void monitor(void *p1, void *p2, void *p3)
{
	atomic_val_t seen = atomic_get(&progress);
	int64_t since = k_uptime_get();
	int64_t next_warn = STALL_MS;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		k_msleep(MONITOR_MS);
		watch_poll("monitor");

		if (atomic_get(&progress) != seen) {
			seen = atomic_get(&progress);
			since = k_uptime_get();
			next_warn = STALL_MS;
		} else if (k_uptime_get() - since >= next_warn) {
			LOG_ERR("STALL: main thread stuck for %u s in %s %s",
				(uint32_t)(next_warn / 1000), step_bus, step);
			next_warn += STALL_MS;
		}
	}
}

static void fill(uint8_t *buf, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		/* xorshift32 */
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;
		buf[i] = (uint8_t)rng;
	}
}

/* Poison the receive area, and the guard behind it that must stay intact. */
static void poison(size_t len)
{
	memset(rx_buf, 0xa5, len);
	memset(rx_buf + len, 0x5a, GUARD);
}

static int check_rx(const struct bus *b, const char *what, const uint8_t *exp, size_t len,
		    bool verbose)
{
	size_t bad = 0;
	size_t first = 0;

	for (size_t i = 0; i < len; i++) {
		if (rx_buf[i] != exp[i] && bad++ == 0U) {
			first = i;
		}
	}
	if (bad != 0U) {
		if (verbose) {
			LOG_ERR("%s %s: %u of %u bytes wrong, first at %u: 0x%02x, want 0x%02x",
				b->name, what, (unsigned int)bad, (unsigned int)len,
				(unsigned int)first, rx_buf[first], exp[first]);
		}
		return -EIO;
	}
	for (size_t i = len; i < len + GUARD; i++) {
		if (rx_buf[i] != 0x5aU) {
			if (verbose) {
				LOG_ERR("%s %s: wrote past the end of the %u-byte buffer",
					b->name, what, (unsigned int)len);
			}
			return -EOVERFLOW;
		}
	}
	return 0;
}

static struct spi_config make_cfg(const struct bus *b, uint32_t hz, uint32_t word,
				  uint32_t mode, bool lsb)
{
	struct spi_config cfg = {
		.frequency = hz,
		.operation = SPI_WORD_SET(word) |
			     (((mode & 1U) != 0U) ? SPI_MODE_CPHA : 0U) |
			     (((mode & 2U) != 0U) ? SPI_MODE_CPOL : 0U) |
			     (lsb ? SPI_TRANSFER_LSB : 0U) |
			     (b->external ? 0U : SPI_MODE_LOOP),
		.peripheral = b->cs,
	};

	return cfg;
}

/*
 * One full-duplex transfer with TX split into three buffers and RX into two
 * at other offsets, so every case also crosses buffer boundaries.
 */
static int xfer(const struct bus *b, const struct spi_config *cfg, size_t len, uint32_t *us)
{
	size_t word = SPI_WORD_SIZE_GET(cfg->operation) / 8U;
	size_t t1 = ROUND_DOWN(len / 3U, word);
	size_t t2 = ROUND_DOWN(len * 2U / 3U, word);
	size_t r1 = MIN(ROUND_DOWN(len / 2U + word, word), len);
	const struct spi_buf txb[] = {
		{.buf = tx_buf, .len = t1},
		{.buf = tx_buf + t1, .len = t2 - t1},
		{.buf = tx_buf + t2, .len = len - t2},
	};
	const struct spi_buf rxb[] = {
		{.buf = rx_buf, .len = r1},
		{.buf = rx_buf + r1, .len = len - r1},
	};
	const struct spi_buf_set txs = {.buffers = txb, .count = ARRAY_SIZE(txb)};
	const struct spi_buf_set rxs = {.buffers = rxb, .count = ARRAY_SIZE(rxb)};
	uint32_t start = k_cycle_get_32();
	int ret = spi_transceive(b->dev, cfg, &txs, &rxs);

	*us = k_cyc_to_us_floor32(k_cycle_get_32() - start);
	return ret;
}

static int loop_case(const struct bus *b, const struct spi_config *cfg, size_t len,
		     const char *what, uint32_t *us_out)
{
	uint32_t us;
	int ret;

	fill(tx_buf, len);
	poison(len);
	ret = xfer(b, cfg, len, &us);
	if (ret != 0) {
		LOG_ERR("%s %s: transfer returned %d", b->name, what, ret);
		return ret;
	}
	if (us_out != NULL) {
		*us_out = us;
	}
	return check_rx(b, what, tx_buf, len, true);
}

/* Unequal TX/RX lengths: RX gets TX, then the zeros sent as TX dummies. */
static int asym_case(const struct bus *b, const struct spi_config *cfg, size_t tx_len,
		     size_t rx_len, const char *what)
{
	const struct spi_buf txb = {.buf = tx_buf, .len = tx_len};
	const struct spi_buf rxb = {.buf = rx_buf, .len = rx_len};
	const struct spi_buf_set txs = {.buffers = &txb, .count = 1U};
	const struct spi_buf_set rxs = {.buffers = &rxb, .count = 1U};
	size_t common = MIN(tx_len, rx_len);
	int ret;

	fill(tx_buf, tx_len);
	poison(rx_len);
	memcpy(exp_buf, tx_buf, common);
	memset(exp_buf + common, 0, rx_len - common);
	ret = spi_transceive(b->dev, cfg, (tx_len != 0U) ? &txs : NULL,
			     (rx_len != 0U) ? &rxs : NULL);
	if (ret != 0) {
		LOG_ERR("%s %s: transfer returned %d", b->name, what, ret);
		return ret;
	}
	return check_rx(b, what, exp_buf, rx_len, true);
}

/* NULL entries: TX sends dummy zeros for them, RX discards them. */
static int null_bufs_case(const struct bus *b, const struct spi_config *cfg, const char *what)
{
	const struct spi_buf txb[] = {
		{.buf = tx_buf, .len = 8},
		{.buf = NULL, .len = 8},
		{.buf = tx_buf + 16, .len = 8},
	};
	const struct spi_buf rxb[] = {
		{.buf = NULL, .len = 4},
		{.buf = rx_buf, .len = 20},
	};
	const struct spi_buf_set txs = {.buffers = txb, .count = ARRAY_SIZE(txb)};
	const struct spi_buf_set rxs = {.buffers = rxb, .count = ARRAY_SIZE(rxb)};
	int ret;

	fill(tx_buf, 24);
	poison(20);
	memcpy(exp_buf, tx_buf + 4, 4);
	memset(exp_buf + 4, 0, 8);
	memcpy(exp_buf + 12, tx_buf + 16, 8);
	ret = spi_transceive(b->dev, cfg, &txs, &rxs);
	if (ret != 0) {
		LOG_ERR("%s %s: transfer returned %d", b->name, what, ret);
		return ret;
	}
	return check_rx(b, what, exp_buf, 20, true);
}

/* Sixteen TX buffers of 1..16 bytes against one RX buffer. */
static int many_bufs_case(const struct bus *b, const struct spi_config *cfg, const char *what)
{
	struct spi_buf txb[16];
	struct spi_buf rxb = {.buf = rx_buf};
	const struct spi_buf_set txs = {.buffers = txb, .count = ARRAY_SIZE(txb)};
	const struct spi_buf_set rxs = {.buffers = &rxb, .count = 1U};
	size_t len = 0;
	int ret;

	ARRAY_FOR_EACH(txb, i) {
		txb[i].buf = tx_buf + len;
		txb[i].len = i + 1U;
		len += i + 1U;
	}
	rxb.len = len;
	fill(tx_buf, len);
	poison(len);
	ret = spi_transceive(b->dev, cfg, &txs, &rxs);
	if (ret != 0) {
		LOG_ERR("%s %s: transfer returned %d", b->name, what, ret);
		return ret;
	}
	return check_rx(b, what, tx_buf, len, true);
}

static int bad_arg(const struct bus *b, const struct spi_config *cfg, size_t len, int want,
		   const char *what)
{
	const struct spi_buf buf = {.buf = tx_buf, .len = len};
	const struct spi_buf_set set = {.buffers = &buf, .count = 1U};
	int ret = spi_write(b->dev, cfg, &set);

	if (ret == want) {
		return 0;
	}
	LOG_ERR("%s %s: returned %d, want %d", b->name, what, ret, want);
	return (ret == 0) ? -EPROTO : ret;
}

static void record(const struct bus *b, const char *what, int ret)
{
	tally.cases++;
	atomic_inc(&progress);
	if (ret != 0) {
		tally.fails++;
		LOG_ERR("FAIL %s %s (%d)", b->name, what, ret);
	}
}

struct group {
	const struct bus *bus;
	const char *name;
	uint32_t cases;
	uint32_t fails;
};

static struct group group_begin(const struct bus *b, const char *name)
{
	step_bus = b->name;
	step = name;
	return (struct group){.bus = b, .name = name, .cases = tally.cases, .fails = tally.fails};
}

static void group_end(const struct group *g)
{
	uint32_t cases = tally.cases - g->cases;
	uint32_t fails = tally.fails - g->fails;

	if (fails == 0U) {
		LOG_INF("%s %s: %u/%u ok", g->bus->name, g->name, cases, cases);
	} else {
		LOG_ERR("%s %s: %u of %u FAILED", g->bus->name, g->name, fails, cases);
	}
}

static void test_modes(const struct bus *b)
{
	struct group g = group_begin(b, "modes");
	char what[40];

	for (uint32_t word = 8U; word <= 16U; word += 8U) {
		for (uint32_t mode = 0U; mode < 4U; mode++) {
			for (int lsb = 0; lsb < 2; lsb++) {
				struct spi_config cfg = make_cfg(b, 6000000U, word, mode, lsb);

				snprintf(what, sizeof(what), "w%u mode%u %s", word, mode,
					 lsb ? "lsb" : "msb");
				record(b, what, loop_case(b, &cfg, 64U, what, NULL));
			}
		}
	}
	group_end(&g);
}

static void test_shapes(const struct bus *b)
{
	/* Around the 32-frame in-flight window, in 8- and 16-bit frames. */
	static const uint16_t lens[] = {1, 2, 3, 31, 32, 33, 62, 63, 64, 65, 66, 127, 1000};
	struct group g = group_begin(b, "shapes");
	const struct spi_config c8 = make_cfg(b, 12000000U, 8U, 0U, false);
	const struct spi_config c16 = make_cfg(b, 12000000U, 16U, 0U, false);
	const struct spi_buf empty_tx = {.buf = tx_buf, .len = 0U};
	const struct spi_buf empty_rx = {.buf = rx_buf, .len = 0U};
	const struct spi_buf_set empty_txs = {.buffers = &empty_tx, .count = 1U};
	const struct spi_buf_set empty_rxs = {.buffers = &empty_rx, .count = 1U};
	char what[40];
	int ret;

	ARRAY_FOR_EACH(lens, i) {
		snprintf(what, sizeof(what), "w8 len %u", lens[i]);
		record(b, what, loop_case(b, &c8, lens[i], what, NULL));
		if ((lens[i] & 1U) == 0U) {
			snprintf(what, sizeof(what), "w16 len %u", lens[i]);
			record(b, what, loop_case(b, &c16, lens[i], what, NULL));
		}
	}
	record(b, "tx 100 rx 40", asym_case(b, &c8, 100U, 40U, "tx 100 rx 40"));
	record(b, "tx 40 rx 100", asym_case(b, &c8, 40U, 100U, "tx 40 rx 100"));
	record(b, "w16 tx 40 rx 100", asym_case(b, &c16, 40U, 100U, "w16 tx 40 rx 100"));
	record(b, "write only", asym_case(b, &c8, 64U, 0U, "write only"));
	record(b, "read only", asym_case(b, &c8, 0U, 64U, "read only"));
	record(b, "null buffers", null_bufs_case(b, &c8, "null buffers"));
	record(b, "16 tx buffers", many_bufs_case(b, &c8, "16 tx buffers"));
	poison(0U);
	ret = spi_transceive(b->dev, &c8, &empty_txs, &empty_rxs);
	record(b, "zero length", (ret != 0) ? ret : check_rx(b, "zero length", exp_buf, 0U, true));
	group_end(&g);
}

/* What the driver programs: 24 MHz over an even divisor, never above the request. */
static uint32_t expected_hz(uint32_t req)
{
	uint32_t div = MAX(DIV_ROUND_UP(INPUT_HZ, req), 2U);

	return INPUT_HZ / ROUND_UP(div, 2U);
}

static void test_rates(const struct bus *b)
{
	static const uint32_t rates[] = {
		30000000, 12000000, 8000000, 6000000, 5000000, 4000000, 3000000,
		2000000, 1000000, 400000, 100000, 10000, 1000, 367,
	};
	struct group g = group_begin(b, "rates");
	char what[40];

	ARRAY_FOR_EACH(rates, i) {
		uint32_t hz = expected_hz(rates[i]);
		size_t len = CLAMP(hz / 400U, 2U, 1024U);
		uint32_t wire = (uint32_t)DIV_ROUND_UP((uint64_t)len * 8U * USEC_PER_SEC, hz);
		struct spi_config cfg = make_cfg(b, rates[i], 8U, 0U, false);
		uint32_t us = 0U;
		uint32_t ratio;
		int ret;

		snprintf(what, sizeof(what), "rate %u Hz", rates[i]);
		ret = loop_case(b, &cfg, len, what, &us);
		/* The last frame cannot arrive before its bits were clocked. */
		if (ret == 0 && (uint64_t)us * 100U < (uint64_t)wire * 97U) {
			LOG_ERR("%s %s: %u us, faster than the %u us wire time at %u Hz",
				b->name, what, us, wire, hz);
			ret = -ERANGE;
		}
		/* At 100 kHz and below the wire dominates: a wrong divisor shows here. */
		if (ret == 0 && hz <= 100000U && us > wire + wire / 2U + 2000U) {
			LOG_ERR("%s %s: %u us, want about %u us at %u Hz",
				b->name, what, us, wire, hz);
			ret = -ERANGE;
		}
		ratio = (uint32_t)((uint64_t)us * 100U / wire);
		LOG_INF("%s rate %8u -> %8u Hz: %4u B in %6u us, wire %6u us, x%u.%02u",
			b->name, rates[i], hz, (unsigned int)len, us, wire,
			ratio / 100U, ratio % 100U);
		record(b, what, ret);
	}
	group_end(&g);
}

static void test_long(const struct bus *b)
{
	static const struct {
		uint32_t hz;
		uint8_t word;
		uint8_t mode;
	} runs[] = {
		{12000000U, 8U, 0U},
		{12000000U, 16U, 3U},
		{1000000U, 8U, 1U},
	};
	struct group g = group_begin(b, "long");
	char what[40];

	ARRAY_FOR_EACH(runs, i) {
		struct spi_config cfg = make_cfg(b, runs[i].hz, runs[i].word, runs[i].mode,
						 false);
		uint32_t us = 0U;
		int ret;

		snprintf(what, sizeof(what), "%u B w%u mode%u %u Hz", LONG_LEN, runs[i].word,
			 runs[i].mode, runs[i].hz);
		ret = loop_case(b, &cfg, LONG_LEN, what, &us);
		if (ret == 0) {
			LOG_INF("%s long %s: %u us, %u kbit/s", b->name, what, us,
				(uint32_t)((uint64_t)LONG_LEN * 8U * 1000U / MAX(us, 1U)));
		}
		record(b, what, ret);
	}
	group_end(&g);
}

static void test_args(const struct bus *b)
{
	struct group g = group_begin(b, "args");
	const struct spi_config good = make_cfg(b, 6000000U, 8U, 0U, false);
	const struct spi_config c16 = make_cfg(b, 6000000U, 16U, 0U, false);

	/* Each case edits a copy of the good config; all must fail cleanly. */
#define BAD(what, len, want, edit) do { \
		struct spi_config c = good; \
		edit; \
		record(b, what, bad_arg(b, &c, len, want, what)); \
	} while (0)

	BAD("hz 0", 4U, -ENOTSUP, c.frequency = 0U);
	BAD("hz 366 (divisor over 65534)", 4U, -EINVAL, c.frequency = 366U);
	BAD("word 9", 4U, -ENOTSUP,
	    c.operation = (c.operation & ~SPI_WORD_SIZE_MASK) | SPI_WORD_SET(9));
	BAD("word 32", 4U, -ENOTSUP,
	    c.operation = (c.operation & ~SPI_WORD_SIZE_MASK) | SPI_WORD_SET(32));
	BAD("peripheral mode", 4U, -ENOTSUP, c.operation |= SPI_OP_MODE_PERIPHERAL);
	BAD("half duplex", 4U, -ENOTSUP, c.operation |= SPI_HALF_DUPLEX);
	BAD("hold on cs", 4U, -ENOTSUP, c.operation |= SPI_HOLD_ON_CS);
	BAD("native cs active high", 4U, -ENOTSUP, c.operation |= SPI_CS_ACTIVE_HIGH);
	BAD("cs 2", 4U, -ENOTSUP, c.peripheral = 2U);
	BAD("w16 odd length", 3U, -EINVAL, c = c16);
#undef BAD

	/* A failed call must not leave the bus locked. */
	record(b, "good transfer after errors",
	       loop_case(b, &good, 64U, "good transfer after errors", NULL));
	group_end(&g);
}

static void test_lock(const struct bus *b)
{
	struct group g = group_begin(b, "lock");
	struct spi_config lock = make_cfg(b, 6000000U, 8U, 0U, false);
	const struct spi_config other = make_cfg(b, 6000000U, 8U, 0U, false);
	int ret;

	lock.operation |= SPI_LOCK_ON;
	record(b, "locked transfer", loop_case(b, &lock, 32U, "locked transfer", NULL));
	record(b, "second locked transfer",
	       loop_case(b, &lock, 32U, "second locked transfer", NULL));
	ret = spi_release(b->dev, &other);
	if (ret != -EINVAL) {
		LOG_ERR("%s release by a non-owner returned %d, want %d", b->name, ret, -EINVAL);
	}
	record(b, "release by non-owner", (ret == -EINVAL) ? 0 : -EPROTO);
	record(b, "release", spi_release(b->dev, &lock));
	record(b, "other config after release",
	       loop_case(b, &other, 32U, "other config after release", NULL));

	/* A failed SPI_LOCK_ON call keeps the lock; only spi_release() frees it. */
	lock.operation = (lock.operation & ~SPI_WORD_SIZE_MASK) | SPI_WORD_SET(16);
	record(b, "failed locked transfer", bad_arg(b, &lock, 3U, -EINVAL, "failed locked transfer"));
	record(b, "release after failure", spi_release(b->dev, &lock));
	/* If the lock leaked, this blocks forever and the monitor reports a STALL. */
	record(b, "other config after failure",
	       loop_case(b, &other, 32U, "other config after failure", NULL));
	group_end(&g);
}

static void run_selftest(const char *label)
{
	uint32_t cases = tally.cases;
	uint32_t fails = tally.fails;
	int64_t start = k_uptime_get();

	LOG_INF("SELFTEST %s: start", label);
	ARRAY_FOR_EACH_PTR(buses, b) {
		test_modes(b);
		test_shapes(b);
		test_rates(b);
		test_long(b);
		test_args(b);
		test_lock(b);
	}
	cases = tally.cases - cases;
	fails = tally.fails - fails;
	if (fails == 0U) {
		LOG_INF("SELFTEST PASS %s (%u cases, %u ms)", label, cases,
			(uint32_t)(k_uptime_get() - start));
	} else {
		LOG_ERR("SELFTEST FAIL %s (%u of %u cases failed)", label, fails, cases);
	}
}

/*
 * Even rounds: 12 MHz, mode rotating every two rounds. Odd rounds: 1 MHz,
 * mode 0, MSB first, starting A5 01 80 3C: a fixed logic analyzer reference.
 */
static void soak_round(struct bus *b, uint32_t round)
{
	static const uint8_t marker[] = {0xa5, 0x01, 0x80, 0x3c};
	bool slow = (round & 1U) != 0U;
	struct spi_config cfg = make_cfg(b, slow ? 1000000U : 12000000U, 8U,
					 slow ? 0U : (round / 2U) % 4U, false);
	uint32_t us;
	int ret;

	watch_poll("soak");
	fill(tx_buf, SOAK_LEN);
	memcpy(tx_buf, marker, sizeof(marker));
	poison(SOAK_LEN);
	ret = xfer(b, &cfg, SOAK_LEN, &us);
	if (ret == 0) {
		ret = check_rx(b, "soak", tx_buf, SOAK_LEN, b->fail < SOAK_ERR_LOGS);
	}
	if (ret == 0) {
		b->ok++;
		return;
	}
	b->fail++;
	if (b->fail <= SOAK_ERR_LOGS) {
		LOG_ERR("soak %s round %u (%s): %d", b->name, round, slow ? "1 MHz" : "12 MHz", ret);
	}
}

static void report(void)
{
	enum clock_control_status s[ARRAY_SIZE(watches)];
	uint32_t offs = 0U;

	k_mutex_lock(&watch_lock, K_FOREVER);
	ARRAY_FOR_EACH(watches, i) {
		s[i] = watches[i].last;
		offs += watches[i].offs;
	}
	k_mutex_unlock(&watch_lock);

	LOG_INF("t=%us soak spi0 %u ok %u fail, spi1 %u ok %u fail | "
		"clk spi0 %s/%s spi1 %s/%s can0 %s/%s | gated %u",
		(uint32_t)(k_uptime_get() / 1000), buses[0].ok, buses[0].fail,
		buses[1].ok, buses[1].fail, status_name(s[0]), status_name(s[1]),
		status_name(s[2]), status_name(s[3]), status_name(s[4]),
		status_name(s[5]), offs);
}

int main(void)
{
	bool second = false;
	uint32_t round = 0U;
	int64_t next;

	LOG_INF("RK3506 SPI test: self-test at boot and at %u s, then a soak every %u ms",
		SELFTEST2_AT_MS / 1000U, SOAK_PERIOD_MS);
	ARRAY_FOR_EACH_PTR(buses, b) {
		if (!device_is_ready(b->dev)) {
			LOG_ERR("%s not ready", b->name);
			return -ENODEV;
		}
		LOG_INF("%s: %s, native CS%u", b->name,
			b->external ? "over the pads, needs MOSI jumpered to MISO"
				    : "internal loopback, no pads", b->cs);
	}
	watch_init();
	k_thread_create(&monitor_thread, monitor_stack, K_THREAD_STACK_SIZEOF(monitor_stack),
			monitor, NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);
	k_thread_name_set(&monitor_thread, "clk_watch");

	run_selftest("boot");
	next = k_uptime_get();
	for (;;) {
		next += SOAK_PERIOD_MS;
		k_sleep(K_TIMEOUT_ABS_MS(next));
		if (!second && k_uptime_get() >= SELFTEST2_AT_MS) {
			second = true;
			run_selftest("t=30s");
			next = k_uptime_get();
		}
		step_bus = "both";
		step = "soak";
		ARRAY_FOR_EACH_PTR(buses, b) {
			soak_round(b, round);
		}
		round++;
		atomic_inc(&progress);
		if (k_uptime_get() < FAST_REPORT_MS || round % 10U == 0U) {
			report();
		}
	}
	return 0;
}
