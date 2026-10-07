// SPDX-License-Identifier: Apache-2.0

#define DT_DRV_COMPAT rockchip_rk3506_spi

#include <errno.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define LOG_LEVEL CONFIG_SPI_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(spi_rk3506);

#include "spi_context.h"
#include "spi_rtio.h"

#define RK3506_SPI_CTRLR0  0x000U
#define RK3506_SPI_ENR     0x008U
#define RK3506_SPI_SER     0x00cU
#define RK3506_SPI_BAUDR   0x010U
#define RK3506_SPI_RXFLR   0x020U
#define RK3506_SPI_SR      0x024U
#define RK3506_SPI_IMR     0x02cU
#define RK3506_SPI_RISR    0x034U
#define RK3506_SPI_ICR     0x038U
#define RK3506_SPI_DMACR   0x03cU
#define RK3506_SPI_TXDR    0x400U
#define RK3506_SPI_RXDR    0x800U

#define RK3506_SPI_RXFLR_MASK GENMASK(6, 0)
#define RK3506_SPI_SR_BUSY BIT(0)
#define RK3506_SPI_SR_TFE  BIT(2)
#define RK3506_SPI_ERROR   (BIT(1) | BIT(2) | BIT(3))
#define RK3506_SPI_INPUT_HZ 24000000U
/*
 * The TRM and vendor HAL say 64 entries; Linux uses 32 for this IP version
 * (0x03110003). With at most 32 frames in flight neither FIFO can overflow,
 * even if the thread is preempted mid-transfer.
 */
#define RK3506_SPI_FIFO_DEPTH 32U

struct rk3506_spi_config {
	DEVICE_MMIO_NAMED_ROM(regs);
	const struct pinctrl_dev_config *pcfg;
	const struct device *clock;
	clock_control_subsys_t spi_clock;
	clock_control_subsys_t bus_clock;
};

struct rk3506_spi_data {
	DEVICE_MMIO_NAMED_RAM(regs);
	struct spi_context ctx;
	/* SCLK actually programmed by the last configure. */
	uint32_t hz;
};

#define DEV_CFG(dev) ((const struct rk3506_spi_config *)(dev)->config)
#define DEV_DATA(dev) ((struct rk3506_spi_data *)(dev)->data)

/*
 * Linux does not know CPU2 owns this block: unless the Linux AMP node lists
 * these clocks, clk_disable_unused() gates them at late_initcall_sync, a few
 * seconds after CPU2 started. An APB access with the pclk gated can stall
 * the bus, so check the CRU (always clocked) before touching the block.
 */
static int rk3506_spi_check_clocks(const struct device *dev)
{
	const struct rk3506_spi_config *config = dev->config;
	const clock_control_subsys_t clocks[] = {config->bus_clock, config->spi_clock};
	int ret;

	ARRAY_FOR_EACH(clocks, i) {
		if (clock_control_get_status(config->clock, clocks[i]) !=
		    CLOCK_CONTROL_STATUS_OFF) {
			continue;
		}
		LOG_WRN("%s: %s clock was gated by another core, re-enabling",
			dev->name, (i == 0U) ? "apb" : "spi");
		ret = clock_control_on(config->clock, clocks[i]);
		if (ret != 0 && ret != -EALREADY) {
			return ret;
		}
	}
	return 0;
}

static int rk3506_spi_configure(const struct device *dev, const struct spi_config *config)
{
	struct rk3506_spi_data *data = dev->data;
	uintptr_t base = DEVICE_MMIO_NAMED_GET(dev, regs);
	uint32_t operation = config->operation;
	uint32_t word_size = SPI_WORD_SIZE_GET(operation);
	uint64_t divisor;
	uint32_t ctrl;

	if (config->frequency == 0U || config->peripheral > 1U ||
	    SPI_OP_MODE_GET(operation) != SPI_OP_MODE_CONTROLLER ||
	    (operation & (SPI_HALF_DUPLEX | SPI_HOLD_ON_CS)) != 0U ||
	    (operation & SPI_LINES_MASK) != SPI_LINES_SINGLE ||
	    (!spi_cs_is_gpio(config) && (operation & SPI_CS_ACTIVE_HIGH) != 0U) ||
	    (word_size != 8U && word_size != 16U)) {
		return -ENOTSUP;
	}

	/* BAUDR accepts even divisors [2, 65534]; never exceed requested speed. */
	divisor = DIV_ROUND_UP((uint64_t)RK3506_SPI_INPUT_HZ, config->frequency);
	divisor = MAX(divisor, 2U);
	divisor = ROUND_UP(divisor, 2U);
	if (divisor > 65534U) {
		return -EINVAL;
	}

	/* Match the vendor controller defaults: one-cycle CS setup and MSB-endian frames. */
	ctrl = ((word_size == 8U) ? BIT(0) | BIT(13) : BIT(1)) | BIT(10) | BIT(11);
	if ((operation & SPI_MODE_CPHA) != 0U) {
		ctrl |= BIT(6);
	}
	if ((operation & SPI_MODE_CPOL) != 0U) {
		ctrl |= BIT(7);
	}
	if ((operation & SPI_TRANSFER_LSB) != 0U) {
		ctrl |= BIT(12);
	}
	if ((operation & SPI_MODE_LOOP) != 0U) {
		ctrl |= BIT(25);
	}

	sys_write32(0U, base + RK3506_SPI_ENR);
	sys_write32(0U, base + RK3506_SPI_SER);
	sys_write32(ctrl, base + RK3506_SPI_CTRLR0);
	sys_write32((uint32_t)divisor, base + RK3506_SPI_BAUDR);
	sys_write32(0U, base + RK3506_SPI_IMR);
	sys_write32(0U, base + RK3506_SPI_DMACR);
	sys_write32(0x7fU, base + RK3506_SPI_ICR);
	data->hz = RK3506_SPI_INPUT_HZ / (uint32_t)divisor;
	return 0;
}

static int rk3506_spi_poll(const struct device *dev, uint32_t reg, uint32_t mask,
			   bool set, int64_t deadline)
{
	uintptr_t base = DEVICE_MMIO_NAMED_GET(dev, regs);

	while (((sys_read32(base + reg) & mask) != 0U) != set) {
		if (k_uptime_get() >= deadline) {
			return -ETIMEDOUT;
		}
		k_busy_wait(1);
	}
	return 0;
}

static int rk3506_spi_transfer(const struct device *dev, int64_t deadline)
{
	struct rk3506_spi_data *data = dev->data;
	struct spi_context *ctx = &data->ctx;
	uintptr_t base = DEVICE_MMIO_NAMED_GET(dev, regs);
	uint32_t bytes = SPI_WORD_SIZE_GET(ctx->config->operation) / 8U;
	size_t frames = ctx->max_count / bytes;
	size_t sent = 0U;
	size_t received = 0U;
	int ret;

	while (received < frames) {
		uint32_t level;

		/* Every TX frame clocks one RX frame back; dummies are zero. */
		while (sent < frames && sent - received < RK3506_SPI_FIFO_DEPTH) {
			uint32_t word = 0U;

			if (spi_context_tx_buf_on(ctx)) {
				word = ctx->tx_buf[0];
				if (bytes == 2U) {
					word |= (uint32_t)ctx->tx_buf[1] << 8;
				}
			}
			sys_write32(word, base + RK3506_SPI_TXDR);
			spi_context_update_tx(ctx, bytes, 1U);
			sent++;
		}

		level = sys_read32(base + RK3506_SPI_RXFLR) & RK3506_SPI_RXFLR_MASK;
		level = MIN(level, sent - received);
		if (level == 0U) {
			if (k_uptime_get() >= deadline) {
				return -ETIMEDOUT;
			}
			continue;
		}
		for (; level > 0U; level--) {
			uint32_t word = sys_read32(base + RK3506_SPI_RXDR);

			if (spi_context_rx_buf_on(ctx)) {
				ctx->rx_buf[0] = (uint8_t)word;
				if (bytes == 2U) {
					ctx->rx_buf[1] = (uint8_t)(word >> 8);
				}
			}
			spi_context_update_rx(ctx, bytes, 1U);
			received++;
		}
		if ((sys_read32(base + RK3506_SPI_RISR) & RK3506_SPI_ERROR) != 0U) {
			return -EIO;
		}
	}

	ret = rk3506_spi_poll(dev, RK3506_SPI_SR, RK3506_SPI_SR_BUSY,
			      false, deadline);
	if (ret != 0) {
		return ret;
	}
	return rk3506_spi_poll(dev, RK3506_SPI_SR, RK3506_SPI_SR_TFE,
			       true, deadline);
}

static int rk3506_spi_transceive(const struct device *dev,
				const struct spi_config *config,
				const struct spi_buf_set *tx_bufs,
				const struct spi_buf_set *rx_bufs)
{
	struct rk3506_spi_data *data = dev->data;
	struct spi_context *ctx = &data->ctx;
	uintptr_t base = DEVICE_MMIO_NAMED_GET(dev, regs);
	uint32_t bytes = SPI_WORD_SIZE_GET(config->operation) / 8U;
	uint64_t duration;
	int64_t deadline;
	int ret;

	spi_context_lock(ctx, false, NULL, NULL, config);
	/*
	 * Set before anything can fail: spi_context_release() reads the
	 * SPI_LOCK_ON flag from ctx->config, which would otherwise still
	 * point at the previous caller's (possibly out of scope) config.
	 */
	ctx->config = config;
	ret = rk3506_spi_check_clocks(dev);
	if (ret != 0) {
		goto out;
	}
	ret = rk3506_spi_configure(dev, config);
	if (ret != 0) {
		goto out;
	}
	if (bytes == 2U) {
		const struct spi_buf_set *sets[] = {tx_bufs, rx_bufs};

		for (size_t i = 0; i < ARRAY_SIZE(sets); i++) {
			if (sets[i] == NULL) {
				continue;
			}
			for (size_t j = 0; j < sets[i]->count; j++) {
				if ((sets[i]->buffers[j].len & 1U) != 0U) {
					ret = -EINVAL;
					goto out;
				}
			}
		}
	}
	spi_context_buffers_setup(ctx, tx_bufs, rx_bufs, bytes);
	if (ctx->max_count == 0U) {
		goto out;
	}

	/* Wire time at the programmed rate plus a generous scheduling margin. */
	duration = DIV_ROUND_UP((uint64_t)ctx->max_count * 8U * 1000U, data->hz);
	duration = MIN(duration + CONFIG_SPI_COMPLETION_TIMEOUT_TOLERANCE + 100U,
		       (uint64_t)INT32_MAX);
	deadline = k_uptime_get() + (int64_t)duration;

	/* Same order as the TRM master flow and Linux: SER while disabled, then enable. */
	spi_context_cs_control(ctx, true);
	/* GPIO CS still needs SER to start the controller; keep native CS0 unmuxed. */
	sys_write32(spi_cs_is_gpio(config) ? BIT(0) : BIT(config->peripheral),
		    base + RK3506_SPI_SER);
	sys_write32(1U, base + RK3506_SPI_ENR);
	ret = rk3506_spi_transfer(dev, deadline);
	/* Disabling also flushes both FIFOs if the transfer failed. */
	sys_write32(0U, base + RK3506_SPI_ENR);
	sys_write32(0U, base + RK3506_SPI_SER);
	spi_context_cs_control(ctx, false);
out:
	spi_context_release(ctx, ret);
	return ret;
}

static int rk3506_spi_release(const struct device *dev,
			      const struct spi_config *config)
{
	struct rk3506_spi_data *data = dev->data;

	if (data->ctx.config != config) {
		return -EINVAL;
	}
	spi_context_unlock_unconditionally(&data->ctx);
	return 0;
}

static int rk3506_spi_init(const struct device *dev)
{
	const struct rk3506_spi_config *config = dev->config;
	struct rk3506_spi_data *data = dev->data;
	uint32_t rate = RK3506_SPI_INPUT_HZ;
	int ret;

	if (!device_is_ready(config->clock)) {
		return -ENODEV;
	}
	ret = clock_control_set_rate(config->clock, config->spi_clock,
				     (clock_control_subsys_rate_t)&rate);
	if (ret != 0) {
		return ret;
	}
	ret = clock_control_on(config->clock, config->bus_clock);
	if (ret != 0 && ret != -EALREADY) {
		return ret;
	}
	ret = clock_control_on(config->clock, config->spi_clock);
	if (ret != 0 && ret != -EALREADY) {
		return ret;
	}
	DEVICE_MMIO_NAMED_MAP(dev, regs, K_MEM_CACHE_NONE);
	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret != 0 && ret != -ENOENT) {
		return ret;
	}
	ret = spi_context_cs_configure_all(&data->ctx);
	if (ret != 0) {
		return ret;
	}
	spi_context_unlock_unconditionally(&data->ctx);
	return 0;
}

static DEVICE_API(spi, rk3506_spi_api) = {
	.transceive = rk3506_spi_transceive,
#ifdef CONFIG_SPI_RTIO
	.iodev_submit = spi_rtio_iodev_default_submit,
#endif
	.release = rk3506_spi_release,
};

#define RK3506_SPI_INIT(n) \
	PINCTRL_DT_INST_DEFINE(n); \
	static const struct rk3506_spi_config rk3506_spi_cfg_##n = { \
		DEVICE_MMIO_NAMED_ROM_INIT(regs, DT_DRV_INST(n)), \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n), \
		.clock = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR_BY_NAME(n, spiclk)), \
		.spi_clock = (clock_control_subsys_t)DT_INST_CLOCKS_CELL_BY_NAME(n, spiclk, id), \
		.bus_clock = (clock_control_subsys_t)DT_INST_CLOCKS_CELL_BY_NAME(n, apb_pclk, id), \
	}; \
	static struct rk3506_spi_data rk3506_spi_data_##n = { \
		SPI_CONTEXT_INIT_LOCK(rk3506_spi_data_##n, ctx), \
		SPI_CONTEXT_INIT_SYNC(rk3506_spi_data_##n, ctx), \
		SPI_CONTEXT_CS_GPIOS_INITIALIZE(DT_DRV_INST(n), ctx) \
	}; \
	SPI_DEVICE_DT_INST_DEFINE(n, rk3506_spi_init, NULL, &rk3506_spi_data_##n, \
				  &rk3506_spi_cfg_##n, POST_KERNEL, \
				  CONFIG_SPI_INIT_PRIORITY, &rk3506_spi_api);

DT_INST_FOREACH_STATUS_OKAY(RK3506_SPI_INIT)
