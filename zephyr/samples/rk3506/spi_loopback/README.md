# SPI0/SPI1 板上测试

本应用在 CPU2 上测试 `drivers/spi/spi_rk3506.c`。
只通过 Zephyr SPI API 访问 SPI0 和 SPI1，应用里不读写 SPI 寄存器。
控制台是 UART1，1500000 8N1，Zephyr 自己复用 RM_IO0/1，Linux 起来之前就有输出。
上板步骤、期望输出和要回传的内容见 SDK 根目录的 `docs/rk3506/testing/SPI.md`。

## 测试内容

1. 自检。开机跑一遍，uptime 30 秒再跑一遍（这时 Linux 已经起来）。
   每条总线 79 项，两条共 158 项：
   - 模式：模式 0～3、8/16 位帧、MSB/LSB 先发，16 项。
   - 形状：1～1000 字节，覆盖 32 帧在途窗口两侧；TX/RX 不等长、只写、只读、
     NULL 缓冲、16 段 TX 缓冲、零长度，27 项。
   - 速率：请求 30 MHz 到 367 Hz 共 14 档，计时并和线上时间比较，14 项。
   - 长传输：4096 字节，12 MHz 两种、1 MHz 一种，3 项。
   - 参数：非法参数必须返回错误码，之后总线仍然可用，11 项。
   - 锁：`SPI_LOCK_ON` 与 `spi_release()`，8 项。
   全部通过时打印 `SELFTEST PASS boot (158 cases, N ms)` 和
   `SELFTEST PASS t=30s (158 cases, N ms)`。
2. 长跑。每秒每条总线收发 256 字节，逐字节比对，接收区后面的 16 字节保护区也要完好。
   偶数轮 12 MHz，模式每两轮换一次；奇数轮 1 MHz、模式 0、MSB 先发，
   数据以 `A5 01 80 3C` 开头，给逻辑分析仪做固定触发。
   前 60 秒每秒一行状态，之后每 10 秒一行。
3. 时钟监视。每 10 ms 读一次 CRU 门控位：SPI0/SPI1 的 pclk 和工作时钟，
   外加 CAN0 的两个时钟作参照。门控一变就打印 `EVENT` 行。
   CAN0 的时钟只在启动时打开一次，之后没人再开，被 Linux 关掉就一直是 off。

主线程卡住 5 秒以上，监视线程打印 `STALL`，并指出卡在哪条总线的哪一组。

## 回环方式

默认两个控制器都用内部回环（CTRLR0 bit25），不经过 pad，不用接线。
节点带 default pinctrl 状态时，该控制器改走 pad，MOSI 必须跳线到 MISO。

## overlay

- `app.overlay`：默认。SPI0/SPI1 都用内部回环。
- `external-spi1.overlay`：SPI1 走 M0 pad，片选用 CSN1（GPIO0_A7）。
  跳线 SoM 78 脚（GPIO0_B1，MOSI）到 76 脚（GPIO0_B2，MISO）。
  CSN0 在 GPIO0_B6，本板用作 CAN0_TX，所以不用。SPI0 仍是内部回环。
- `external-pins.overlay`：只做编译检查。它关掉 CAN0，把 SPI0/SPI1 的 pad 全部复用出去。
  不要烧。CAN0 的两根脚正好是 SPI1_CSN0（GPIO0_B6）和 SPI0_CLK（GPIO0_C0）。
- `gpio-cs-build.overlay`：只做编译检查，验证 GPIO 片选路径，必须同时加 `CONFIG_GPIO=y`。
  片选用 GPIO0_B5（SoM 66 脚），底板上是普通 IO，Linux 没用。没上板验证过。
  以前用的 GPIO0_A3 是 Linux 的 PWM0_CH2（LCD 背光），不能用。

各脚归属的完整核对见 SDK 根目录 `docs/rk3506/testing/SPI.md` 第 9 节。

在 SDK 根目录构建内部回环；走 pad 时再加下面的 overlay 环境变量：

```sh
RK_AMP_ZEPHYR_APP=zephyr/samples/rk3506/spi_loopback ./build.sh amp

RK_AMP_ZEPHYR_APP=zephyr/samples/rk3506/spi_loopback \
RK_AMP_ZEPHYR_EXTRA_DTC_OVERLAY_FILE=external-spi1.overlay \
./build.sh amp
```

## 驱动范围

- 只做控制器模式。轮询，没有中断，没有 DMA。
- 帧宽 8 位或 16 位。16 位时每个缓冲长度必须是偶数，否则返回 `-EINVAL`。
- 时钟源固定为 24 MHz OSC，分频只能是 2～65534 的偶数，实际速率不超过请求值。
  最高 12 MHz，最低 366 Hz（请求值至少 367 Hz，366 Hz 返回 `-EINVAL`）。
- 片选：原生 CS0/CS1，低有效。也可以用 GPIO 片选（`cs-gpios`）。
  用 GPIO 片选时驱动仍要置 SER0 才能启动传输，所以原生 CS0 不能同时复用到 pad，
  否则那根脚也会跟着拉低。
- 不支持，返回 `-ENOTSUP`：外设模式、半双工、双线/四线、`SPI_HOLD_ON_CS`、
  原生片选高有效、peripheral 大于 1、帧宽不是 8/16。
- 每次传输的超时 = 线上时间 + 300 ms，超时返回 `-ETIMEDOUT`。
  FIFO 溢出或下溢等错误位返回 `-EIO`。

## 和 Linux 的时钟冲突

旧 Linux 基线不知道 SPI0/SPI1/CAN0 归 CPU2。Linux 侧这些节点都是 disabled，
没人 enable 这些时钟，`clk_disable_unused()` 会在 late_initcall_sync 把它们门控掉。
驱动每次传输前先查 CRU（CRU 本身不会被门控），发现被关就重新打开并打印一次警告：

```
spi@ff120000: apb clock was gated by another core, re-enabling
```

这只是兜底：Linux 恰好在传输中途关时钟，这次传输仍会失败，甚至可能卡住总线。
根治是在 Linux AMP 节点的 `clocks` 里列出这些时钟，由 `rockchip_amp` 驱动持有，
改动在 SDK 根目录的 `kernel-6.1/arch/arm/boot/dts/OK3506-S-MINI_amp_nand.dts`，
已包含在 2026-09-28 重编的 boot.img 中；新基线应无关时钟事件，详见测试指南第 5 节。
