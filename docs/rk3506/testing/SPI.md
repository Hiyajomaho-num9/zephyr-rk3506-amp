# RK3506 SPI 驱动上板测试

最后更新：2026-10-06（新增内部回环完整升级包；尚未新增板上结果）。

本文所有源码路径均相对开发根目录。当前 boot.img 已含 fix A，优先按第 5 节验收；第 3/4 节的关时钟现象是旧 Linux 基线的对照。
各应用和镜像的版本、用途见 [测试对照](README.md)。10/06 最新完整包已使用 SPI 内部回环；9/28 包是 pingpong。

## 0. 结论

- SPI 驱动已整合：`zephyr/drivers/spi/spi_rk3506.c`，SPI0 和 SPI1 都能用。
- 只做控制器模式。轮询收发，没有中断，没有 DMA。
  8/16 位帧，模式 0～3，MSB/LSB 先发，速率 366 Hz～12 MHz。
  完整的能力和限制见 `zephyr/samples/rk3506/spi_loopback/README.md` 的“驱动范围”。
- 测试应用：`zephyr/samples/rk3506/spi_loopback`。内部/SPI1 引脚两种 AMP 已编译；10/06 新增内部回环完整包。
  新编译仍有已知 `__rbit` 告警，没有板上验收结果。
- 发现 Linux 起来几秒后会关掉 SPI0、SPI1、CAN0 的时钟。SPI 驱动每次传输前检查，被关了就重开。
  这只是兜底。根治的 DTS 改动（下文叫 fix A）已编入 2026-09-28 的 boot.img，见第 5 节。
- 顺带发现两件和 SPI 无关、但影响现有测试的事，见第 8 节：
  Linux 会把 GPIO0_A2 切成 PWM0_CH1（影响开关机压测）；CAN0 时钟同样会被关（影响 pingpong）。
- 按飞凌的引脚复用对照表和板上 DTB，把 Zephyr 用到的脚逐根核对了一遍，见第 9 节。
  SPI 这两版镜像只碰 UART1 和 SPI1 的脚，Linux 都没用，不受影响。
  踩到一个大坑：原来 gpio_basic_api 的环回脚 gpio2 pin4/5 是 SPI NAND 的两根数据线，已改到 GPIO3_A6/A7。

当前推荐：使用已含 fix A 的 boot.img，结合第 3/4 节测试项目和第 5 节的新基线期望验收。

## 1. 镜像

当前推荐完整包：`output/releases/update-20261006-020523Z-spi-internal/update.img`，71,819,850 字节。
SPI0/SPI1 都走内部回环，不需接线；包含新编 Linux 时钟/PWM 修复和 userdata。
完整升级使用 RKDevTool 固件升级流程；下节只烧 AMP 的步骤适用于单独的 amp.img。

```text
79165572a9280bd39204b9a77f6efb2fde84bac9ae5c881ea84b2d3441928ebf  update.img
f41d280fe74a106712640e3463314a86dc2ae98586131bd3f583c69240f726db  amp.img
```

同目录保存 boot.img、amp.img、配置和校验清单。按第 3 节的自检/长跑项目测试，
Linux 时钟期望用第 5 节的新基线（gated 0），没有 RPMsg 端点。

历史测试镜像目录（相对开发根）：`output/releases/`：

| 文件 | 用在 | 内容 |
|---|---|---|
| `amp-spi-loopback-20260925.img` | 第 3、5 节 | SPI0、SPI1 都走内部回环，不接线 |
| `amp-spi1-external-20260925.img` | 第 4 节 | SPI0 内部回环，SPI1 走引脚，要跳线 |

两个都是 62976 字节。sha256：

```
cae754d5800fa4bc59a035b9e3d54e12a91256e91c5e404bf3a140b380abbc5a  amp-spi-loopback-20260925.img
7496f0f1bd157f602cc669b6aa36acb4663d74b74e73b533b70185a6886b308d  amp-spi1-external-20260925.img
```

同目录的 `zephyr-spi-loopback-20260925.bin`（`f86730e0…`）和
`zephyr-spi1-external-20260925.bin`（`c1808319…`）是镜像里的裸固件，只做对照，不用烧。

两版都含工作树里全部未提交的改动。编法和 9/24 那两版一样：
当时使用 SDK gcc 10.2.1 在临时目录打包；这是历史产物说明。当前统一从 SDK 根调用 `./build.sh amp`，命令见 `docs/rk3506/BUILD.md`。

## 2. 烧写和串口

1. 拷到 Windows 后先校验，输出要和上面一致：

   ```
   certutil -hashfile amp-spi-loopback-20260925.img SHA256
   ```

2. RKDevTool 只烧 amp 分区，别的分区不动。
   地址 `0x00059800`。RKDevTool 地址栏按扇区（512 字节）算，换成字节是 `0x0B300000`。
3. 串口接 UART1（RM_IO0/1，即 GPIO0_A0/A1），1500000 8N1。
   Zephyr 的输出只在这个口，Linux 控制台还是 ttyFIQ0。
4. 上电后 U-Boot 日志里要有 `AMP: Brought up cpu[f02]`。
5. 这两版都没有 RPMsg。Linux 下 `rk3506_rpmsg_char_ping` 超时是正常的。

测完把 amp 分区恢复为已选定的 pingpong 版本。当前源码对应的归档位于
`output/releases/update-20260928-025357Z-sdk-root/amp.img`，本版本也仍待上板。
`amp-pingpong-production.img` 和 `amp-pingpong-20260924-gicsafe.img` 是历史回退/对照材料，
不能把旧版可用直接算作新版验收。

## 3. 第一轮：内部回环，Linux 不动

目的：驱动本身对不对；Linux 关时钟时驱动能不能兜住。
镜像 `amp-spi-loopback-20260925.img`，不接线。至少跑 2 分钟，有空跑 10 分钟以上。

### 3.1 开机自检

每行前面还有 `[00:00:00.123,456]` 这样的时间戳，下面省掉了。`<wrn>` 是黄色，`<err>` 是红色。

```
[RK3506][CLEAN-ZEPHYR] soc_early_init_hook
*** Booting Zephyr OS build cbb9882a6273 ***
<inf> spi_test: RK3506 SPI test: self-test at boot and at 30 s, then a soak every 1000 ms
<inf> spi_test: spi0: internal loopback, no pads, native CS0
<inf> spi_test: spi1: internal loopback, no pads, native CS0
<inf> spi_test: clocks (pclk/clk): spi0 on/on, spi1 on/on, can0 canary on/on
<inf> spi_test: SELFTEST boot: start
<inf> spi_test: spi0 modes: 16/16 ok
<inf> spi_test: spi0 shapes: 27/27 ok
<inf> spi_test: spi0 rate 30000000 -> 12000000 Hz: 1024 B in   NNNN us, wire    683 us, xN.NN
（共 14 行速率，见下表）
<inf> spi_test: spi0 rates: 14/14 ok
<inf> spi_test: spi0 long 4096 B w8 mode0 12000000 Hz: NNNN us, NNNN kbit/s
<inf> spi_test: spi0 long 4096 B w16 mode3 12000000 Hz: NNNN us, NNNN kbit/s
<inf> spi_test: spi0 long 4096 B w8 mode1 1000000 Hz: NNNN us, NNNN kbit/s
<inf> spi_test: spi0 long: 3/3 ok
<inf> spi_test: spi0 args: 11/11 ok
<inf> spi_test: spi0 lock: 8/8 ok
（spi1 再来一遍）
<inf> spi_test: SELFTEST PASS boot (158 cases, N ms)
```

速率 14 档：

| 请求 Hz | 实际 Hz | 字节 | 线上时间 us | 耗时上限 us |
|---|---|---|---|---|
| 30000000 | 12000000 | 1024 | 683 | 不查 |
| 12000000 | 12000000 | 1024 | 683 | 不查 |
| 8000000 | 6000000 | 1024 | 1366 | 不查 |
| 6000000 | 6000000 | 1024 | 1366 | 不查 |
| 5000000 | 4000000 | 1024 | 2048 | 不查 |
| 4000000 | 4000000 | 1024 | 2048 | 不查 |
| 3000000 | 3000000 | 1024 | 2731 | 不查 |
| 2000000 | 2000000 | 1024 | 4096 | 不查 |
| 1000000 | 1000000 | 1024 | 8192 | 不查 |
| 400000 | 400000 | 1000 | 20000 | 不查 |
| 100000 | 100000 | 250 | 20000 | 32000 |
| 10000 | 10000 | 25 | 20000 | 32000 |
| 1000 | 1000 | 2 | 16000 | 26000 |
| 367 | 366 | 2 | 43716 | 67574 |

- 每档耗时不能低于线上时间的 97%，否则报 -34。
- 100 kHz 及以下还不能超过上限，否则也报 -34。分频算错会在这里暴露。
- 高速档行尾的倍数 `xN.NN` 大于 1 是正常的。这版 I-cache 和 D-cache 都没开，CPU 喂 FIFO 跟不上 12 MHz。
  这些数请原样发我。

### 3.2 Linux 关时钟（这一轮最要紧的观察点）

Linux 起来几秒后，也就是它 dmesg 里打印 `clk: Disabling unused clocks` 的那一刻，UART1 出现：

```
<wrn> spi_test: EVENT t=XXXX ms spi0.pclk on -> off (seen by monitor)
<wrn> spi_test: EVENT t=XXXX ms spi0.clk on -> off (seen by monitor)
<wrn> spi_test: EVENT t=XXXX ms spi1.pclk on -> off (seen by monitor)
<wrn> spi_test: EVENT t=XXXX ms spi1.clk on -> off (seen by monitor)
<wrn> spi_test: EVENT t=XXXX ms can0.hclk on -> off (seen by monitor)
<wrn> spi_test: EVENT t=XXXX ms can0.clk on -> off (seen by monitor)
```

1 秒之内，下一轮长跑发现时钟被关，驱动重新打开：

```
<wrn> spi_rk3506: spi@ff120000: apb clock was gated by another core, re-enabling
<wrn> spi_rk3506: spi@ff120000: spi clock was gated by another core, re-enabling
<wrn> spi_test: EVENT t=YYYY ms spi0.pclk off -> on (seen by soak)
<wrn> spi_test: EVENT t=YYYY ms spi0.clk off -> on (seen by soak)
<wrn> spi_rk3506: spi@ff130000: apb clock was gated by another core, re-enabling
<wrn> spi_rk3506: spi@ff130000: spi clock was gated by another core, re-enabling
<wrn> spi_test: EVENT t=YYYY ms spi1.pclk off -> on (seen by monitor)
<wrn> spi_test: EVENT t=YYYY ms spi1.clk off -> on (seen by monitor)
```

- `seen by` 是 monitor 还是 soak 都对，行的先后也可能略有不同。
- 这一组只应出现一次。
- 之后状态行是 `clk spi0 on/on spi1 on/on can0 off/off | gated 6`。
  这版里没人再开 CAN0 的时钟，所以 can0 一直是 off。
- 紧挨着重开的那一条状态行可能还显示 `spi1 off/off`。下一条 EVENT 就变回 on，正常。
- 这一刻长跑允许失败 1 次（Linux 恰好在传输中间关时钟）。有的话告诉我。
- XXXX 是 CPU2 的开机时间。请把它和 dmesg 里那行的时间一起发我。

### 3.3 长跑

```
<inf> spi_test: t=45s soak spi0 NN ok 0 fail, spi1 NN ok 0 fail | clk spi0 on/on spi1 on/on can0 off/off | gated 6
```

- 每秒每条总线收发 256 字节，逐字节比对。偶数轮 12 MHz，模式 0～3 轮换；奇数轮 1 MHz，模式 0。
- 前 60 秒每秒一行，之后每 10 秒一行。
- 30 秒时再跑一遍自检，结尾是 `SELFTEST PASS t=30s (158 cases, N ms)`。这时 Linux 已经起来了。
- 两个 `fail` 计数要一直是 0。

### 3.4 Linux 侧命令

Linux 起来 1 分钟以后，在 ttyFIQ0 上跑：

```sh
cat /proc/cmdline
dmesg | grep -i -E 'unused clocks|rockchip-amp|ff931000'
echo spi_gate=$(( ($(devmem 0xff9a0830 32) >> 10) & 0xf ))
echo can0_gate=$(( ($(devmem 0xff9a0834 32) >> 4) & 0x3 ))
echo a2_mux=$(( ($(devmem 0xff950000 32) >> 8) & 0xf ))
printf 'rmio2=0x%x\n' $(( $(devmem 0xff910088 32) & 0x7f ))
wc -c < /proc/device-tree/rockchip-amp/clocks
cat /proc/device-tree/pwm@ff931000/status; echo
ls /sys/rk_amp/
mount | grep -q debugfs || mount -t debugfs none /sys/kernel/debug
grep -E 'clk_spi[01]|clk_can0' /sys/kernel/debug/clk/clk_summary
```

第一轮的期望：

| 项 | 期望 | 含义 |
|---|---|---|
| `/proc/cmdline` | 没有 `clk_ignore_unused` | 有的话 Linux 不关时钟，3.2 那组不会出现 |
| dmesg | 有 `clk: Disabling unused clocks`，有 `ff931000.pwm: No active pinctrl state` | 前者是关时钟的时刻，后者见 8.1 |
| spi_gate | 0 | Linux 关过，Zephyr 又打开了 |
| can0_gate | 3 | Linux 关了，没人再开 |
| a2_mux、rmio2 | 7、0x1f | GPIO0_A2 被切成 PWM0_CH1，见 8.1 |
| clocks 字节数 | 48 | 板上 DTB 的 AMP 节点只列了 6 个时钟 |
| pwm@ff931000 | okay | 同上，见 8.1 |
| `/sys/rk_amp/` | 有 `boot_cpu` | rockchip_amp 的 probe 成功 |
| clk_summary | pclk_spi0/1、clk_spi0/1、hclk_can0、clk_can0 的 enable 和 prepare 计数都是 0；clk_spi0、clk_spi1 的 rate 是 24000000 | Linux 不知道有人在用这些时钟；24 MHz 是 Zephyr 设的 |

不要在 Linux 下用 devmem 读 SPI0（0xff120000）、SPI1（0xff130000）、CAN0 控制器本身的寄存器。
时钟被关的时候读它们，Linux 这边的 CPU 也会挂死。上面只读 CRU、IOC、RM_IO 寄存器，这些没有这个问题。

### 3.5 判定

1. `SELFTEST PASS boot` 和 `SELFTEST PASS t=30s` 都出现。
2. 两个 fail 计数一直是 0（3.2 那一刻最多 1 次）。
3. 3.2 那组 EVENT 和警告只出现一次。
4. 没有 `STALL`，控制台没停。

可选：断电重启 10 次，每次看 boot 自检、3.2 那一组、2 分钟长跑。记下有没有卡死。

## 4. 第二轮：SPI1 走引脚（要跳线）

目的：引脚复用、真实 pad 上的收发和时序。

### 4.1 先看底板

这几根线在底板上接到“SPI1电路”。我这里没有底板原理图，不知道上面挂了什么。
飞凌的引脚复用对照表只写了网络名（SPI1_CLK 这些），没写接了什么器件。

- 挂了任何器件（尤其 SPI flash），先别做，把器件和接法告诉我。
  器件会驱动 MISO，和跳线冲突。测试数据是随机的，flash 可能把它当成写或擦除命令。
- 只引到排针或测试点、没有器件，可以做。

### 4.2 接线

| 信号 | 焊盘 | SoM 脚 | 接法 |
|---|---|---|---|
| SCLK | GPIO0_B0 | 80 | 可接逻辑分析仪 |
| MOSI | GPIO0_B1 | 78 | 跳线到 76 脚 |
| MISO | GPIO0_B2 | 76 | 跳线到 78 脚 |
| CSN1 | GPIO0_A7 | 95 | 可接逻辑分析仪 |

片选用 CSN1。CSN0 在 GPIO0_B6（67 脚），本板给 CAN0_TX 用了，不碰。
CAN0_RX 在 GPIO0_C0（89 脚，底板上叫 SPI0_CLK）。接跳线时别动 67、89 上 CAN 收发器的线。
SPI0 不做走引脚的测试，也是因为 89 脚被 CAN0 占了。

### 4.3 期望

镜像 `amp-spi1-external-20260925.img`。开头这两行变成：

```
<inf> spi_test: spi0: internal loopback, no pads, native CS0
<inf> spi_test: spi1: over the pads, needs MOSI jumpered to MISO, native CS1
```

其余和第一轮一样：两次 SELFTEST PASS，同样一组关时钟的 EVENT 和警告，fail 为 0。

Linux 下查引脚复用。这是 Zephyr 启动时设的，Linux 不碰这几根脚：

```sh
printf 'b0_b2=0x%x\n' $(( $(devmem 0xff950008 32) & 0xfff ))
echo a7_mux=$(( ($(devmem 0xff950004 32) >> 12) & 0xf ))
```

期望 `b0_b2=0x222`、`a7_mux=2`（功能 2 就是 SPI1）。

### 4.4 拔跳线

1. 长跑中（t 过 60 秒以后）拔掉 78-76 的跳线。
2. spi1 开始每轮失败，错误码 -5。spi0 不受影响：

   ```
   <err> spi_test: spi1 soak: NNN of 256 bytes wrong, first at 0: 0x00, want 0xa5
   <err> spi_test: soak spi1 round N (12 MHz): -5
   ```

   读到 0x00 还是 0xff，取决于 MISO 脚的上下拉。详细报错只打前 10 次，之后看状态行里 spi1 的 fail 计数。
3. 插回跳线。spi1 的 ok 计数恢复增长，fail 不再涨。

可选：不插跳线上电。spi0 应该全过，spi1 大部分失败：

```
<err> spi_test: SELFTEST FAIL boot (62 of 158 cases failed)
```

62 还是 63，取决于 MISO 悬空读成 0 还是 1（“read only”那项期望全 0）。
参数检查、锁、只写、零长度这些不看数据的项仍然通过。
这能证明第二版确实走了引脚，不是误用了内部回环。

### 4.5 逻辑分析仪（可选）

- 通道：SCLK（80）、MOSI（78）、CSN1（95），地线接板上 GND。
- 触发：CSN1 下降沿。spi1 每秒传一次，1 MHz 和 12 MHz 交替。
- 1 MHz 那一轮：模式 0，MSB 先发，8 位，256 字节，约 2 ms，开头是 `A5 01 80 3C`。解码器按这个设。
- 12 MHz 那一轮：模式 0、1、2、3 轮换。采样率至少 50 MSa/s，最好 100 MSa/s。量一下 SCLK 频率和空闲电平就行。
- 一次传输里 CS 一直是低。字节之间 SCLK 可能停一下，那是 CPU 喂 FIFO 的间隙，正常。

## 5. 当前推荐基线：Linux 持有时钟（fix A）

2026-09-28 和 2026-10-06 的 boot.img 均已包含时钟持有和 PWM0_CH1 禁用。原来的仓库/SDK 同步步骤已退役。
源码直接位于开发根目录的 `kernel-6.1/arch/arm/boot/dts/OK3506-S-MINI_amp_nand.dts`。

在开发根目录重建：

```sh
export PATH="$HOME/venv-zephyr-py312/bin:$HOME/.local/bin:$PATH"
export Python3_EXECUTABLE="$HOME/venv-zephyr-py312/bin/python"
./build.sh kernel
RK_AMP_ZEPHYR_APP=zephyr/samples/rk3506/spi_loopback ./build.sh amp
```

新 boot/amp 位于 `output/firmware/`。如只更新这两个分区，RKDevTool 扇区地址为 boot `0x00004800`、amp `0x00059800`。
归档镜像在 `output/releases/`；10/06 构建前备份在本次 validation 目录的 before/。
当前 10/06 完整包使用 SPI 内部回环；9/28 完整包的 AMP 是 pingpong。

UART1 期望：没有关时钟的 EVENT 或 `clock was gated` 警告；状态一直是
`clk spi0 on/on spi1 on/on can0 on/on | gated 0`；两次 SELFTEST PASS，fail 为 0。

Linux 下执行第 3.4 节的命令，新基线应满足：

| 项 | 期望 |
|---|---|
| dmesg | 仍有 Disabling unused clocks；无 AMP 时钟启用失败、无 ff931000.pwm 报错 |
| spi_gate / can0_gate | 0 / 0 |
| a2_mux | 0 |
| clocks 字节数 | 96 |
| pwm@ff931000 | disabled |
| /sys/rk_amp/ | 有 boot_cpu |
| clk_summary | 六个 SPI/CAN 时钟的 enable 和 prepare 计数均为 1 |

第 3 节的 gated 6、CAN off/off、PWM 复用 7 等期望只适用于旧 Linux 基线。
如果 AMP probe 失败，CPU2 仍由 U-Boot 启动，但时钟可能无人持有，需要回查 dmesg。
本节只验证时钟/PWM 修复；Linux GIC 清 AMP IRQ 使能问题仍独立待修。

## 6. 错误码和特殊情况

| 码 | 名字 | 在这个测试里的意思 |
|---|---|---|
| -5 | EIO | 收到的数据不对，或控制器报了 FIFO 错误 |
| -116 | ETIMEDOUT | 传输没在“线上时间 + 300 ms”内完成 |
| -34 | ERANGE | 耗时和线上时间对不上，分频或时钟源不对 |
| -139 | EOVERFLOW | 写过了接收缓冲的末尾 |
| -71 | EPROTO | 本该报错的调用返回了 0，或 release 的结果不对 |
| -22 | EINVAL | 参数错（args、lock 组里本来就期望它的项除外） |
| -134 | ENOTSUP | 配置不支持（args 组里本来就期望它的项除外） |
| -19 | ENODEV | 设备没就绪。启动时打印 `spiN not ready`，程序退出 |

失败时先打一行具体原因，再打 `FAIL spiN 项名 (错误码)`，组结尾是 `spiN 组名: X of Y FAILED`。

特殊情况：

- 控制台突然不动，也没有 STALL：多半是 CPU2 在时钟被关的那一刻正在访问 SPI 寄存器，总线挂死，只能断电。
  记下最后几行和大概时间。发生概率尚无统计；如果 boot 自检刚好和 Linux 关时钟重叠，概率会高很多。
- `STALL: main thread stuck for N s in spiX 组名`：主线程卡住，但 CPU 还活着，是软件问题（比如锁没放）。
  每 5 秒重复一次。把这些行发我。
- -116 只出现在 12 MHz 的 4096 字节项：CPU 喂 FIFO 太慢。把耗时发我。
- -34 只出现在 t=30s 那次自检：Linux 起来后改了 SPI 的时钟源或分频。
- `clock was gated` 警告每秒都出：Linux 在反复关时钟，不是一次性的。
- 状态行的 `gated` 大于 6：时钟被关了不止一次。
- 启动时出现 `canary ... clock_control_on() = N`：CRU 驱动的门控表有问题。

## 7. 要发回给我的

1. UART1 日志：从上电到至少 t=70s，最好 2 分钟以上，不要截断。每轮一份；第二轮要包括拔、插跳线那一段。
2. 3.4 那组 Linux 命令的输出，每轮都要。第二轮再加 4.3 那两条。
3. U-Boot 里 `AMP:` 开头的几行。
4. 逻辑分析仪截图（做了的话）：1 MHz 那一轮的解码，12 MHz 那一轮的波形。
5. 卡死记录：第几次上电，最后几行日志。
6. 烧进去的镜像的 `certutil` 输出。
7. 9.5 那组命令的输出。另外告诉我，以前有没有用老的 overlay（gpio2 pin4/5）跑过 gpio_basic_api。

## 8. 旧 Linux 基线的附带发现

### 8.1 GPIO0_A2 被 Linux 切成 PWM0_CH1（影响开关机压测）

- 旧基线 DTB（SDK 6 月 3 日编译）里 `pwm@ff931000`（DTS 标签 `pwm0_4ch_1`）是 okay，
  pinctrl 只有 `default` 一个状态，内容是 `rm-io2-pwm0-ch1`，也就是 GPIO0_A2。内核开了 `CONFIG_PWM_ROCKCHIP=y`。
- 驱动核心在 probe 之前先套 default 状态：A2 的 IOMUX 切到 7（RM_IO），RM_IO2 选 0x1f（PWM0_CH1）。
- pwm-rockchip 要的是 `active` 状态，找不到就 probe 失败（`No active pinctrl state`）。失败后复用不会还原。
- 结果：从 Linux 套上这个状态起，引脚由 PWM0_CH1 驱动，电平取决于 PWM 的空闲输出，Zephyr 写 GPIO0_A2 到不了引脚。
  压测时序里 50 秒那次拉低发生在 Linux 起来之后，很可能根本没出现在引脚上。
- 待回的 `amp-powercycle-gpio0a2-20260924.img` 结果要按这个看。
  第一轮 3.4 的 `a2_mux`、`rmio2` 就能确认：7 和 0x1f 就是被切走了，0 才是 GPIO。
- debugfs 的 `pinmux-pins` 里 pin 2 可能显示 UNCLAIMED（probe 失败时把占用释放了），不能用它判断，要看寄存器。
- 修法：`&pwm0_4ch_1 { status = "disabled"; }`，和 fix A 在同一个 DTS 里，第三轮的 boot.img 会一起带上。
- `docs/rk3506/history/external-repo-ZCODE-SYNC.md` §14 以前写“`pwm@ff171000` 本来就是 disabled，眼下板上无冲突”，那是看错了节点，已更正。

### 8.2 CAN0 的时钟同样会被关（影响 pingpong 生产版）

- pingpong 里 CAN0 归 CPU2，CAN 线程响应 RPMsg 的 `can:tx:`、`can:rx:`、`can:txrx:` 请求。
- Linux 同样会关 CAN0 的 hclk 和工作时钟。第一轮的 `can0_gate=3` 就能证实。
- CAN 驱动没有 SPI 驱动那样的兜底。Linux 起来后再发 CAN 请求，CPU2 会在 hclk 关着的情况下访问 CAN0 寄存器，
  可能直接卡死。
- fix A 一并修掉。fix A 上板之前，pingpong 下的 CAN 功能不要当真。

### 8.3 还在等的结果

- `amp-pingpong-20260924-gicsafe.img`（zephyr.bin 前缀 `78128fb6`）：RPMsg ping 通不通，
  `devmem 0xff5818b0 8` 是不是 `0x04`。
- `amp-powercycle-gpio0a2-20260924.img`（前缀 `736207d9`）：连同 8.1 的 `a2_mux`、`rmio2` 一起看。

## 9. 引脚分配（9/25 核对）

依据两份东西：

- 飞凌《2-FET3506B-S 引脚复用对照表-20251028.xlsx》：SoM 脚号、底板网络名、核心板上下拉、特殊说明。
- 板上在用的 DTB（8.1 那份，SDK 6 月 3 日编译）。反编译后逐个节点查 pinctrl、`*-gpios` 和 RM_IO 功能号。
  只算 okay 的节点。

下文的“SoM 脚”都是核心板连接器的脚号。飞凌在线手册抓不下来，我不知道它们在底板排针上的位置，接线前请你对一下。

### 9.1 Zephyr 用到的脚

| 用途 | 焊盘 | SoM 脚 | 底板网络 | Linux | 结论 |
|---|---|---|---|---|---|
| UART1 控制台 | GPIO0_A0、A1 | 86、72 | UART1_TX、UART1_RX | AMP 节点替 CPU2 设的 RM_IO 功能 1、2 | 归 CPU2 |
| 开关机压测 | GPIO0_A2 | 85 | PWM0 | 旧 DTB 切成 PWM0_CH1；新 boot 已禁用 | 要关 pwm0_4ch_1，见 8.1 |
| CAN0_TX | GPIO0_B6 | 67 | SPI1_CSN0 | 没人用，Linux 的 can@ff320000 是 disabled | 归 CPU2 |
| CAN0_RX | GPIO0_C0 | 89 | SPI0_CLK | 同上 | 归 CPU2 |
| SPI1 走引脚 | GPIO0_B0、B1、B2、A7 | 80、78、76、95 | SPI1_CLK、MOSI、MISO、CSN1 | 没人用 | 第 4 节 |
| GPIO 片选（只编译） | GPIO0_B5 | 66 | GPIO0_B5，普通 IO | 没人用 | 能用，没上板 |
| gpio_basic_api 环回 | GPIO3_A6、A7 | 129、128 | GPIO3_A6、A7，普通 IO | 没人用 | 新的环回脚 |

- GPIO0_B5、GPIO3_A6、GPIO3_A7 都是 3.3V，核心板上没有上下拉。
- GPIO3_A6/A7 只有 disabled 的 SAI2 节点提到。第二路网口 RMII1 按芯片复用也在这几根脚上，它的节点同样是 disabled。
- SPI0 走引脚要用 GPIO0_C0～C3 和 B7（89、88、96、91、97 脚）。C0 是 CAN0_RX，所以 SPI0 只做内部回环。

### 9.2 Linux 占着的脚，Zephyr 不要碰

| 焊盘 | SoM 脚 | Linux 用途 |
|---|---|---|
| GPIO0_A3 | 71 | PWM0_CH2，LCD 背光（pwm@ff932000） |
| GPIO0_A4、A5 | 68、69 | I2C2，挂两颗 RTC（rx8010@32、pcf8563@51），核心板上有 2K 上拉 |
| GPIO0_A6 | 101 | TF 卡检测（mmc@ff480000 的 cd-gpios） |
| GPIO0_B3、B4 | 75、77 | I2C0（i2c@ff040000） |
| GPIO0_C4 | 92 | 网口 PHY 复位（ethernet@ff4c8000 的 reset-gpio） |
| GPIO0_C5 | 94 | PWM0_CH0，CPU 电压（pwm@ff930000） |
| GPIO0_C6、C7 | 12、13 | UART0，Linux 控制台 ttyFIQ0 |
| GPIO0_D0 | 98 | 心跳灯 |
| GPIO1_C1、C2、D0、D1 | 53、54、61、62 | 音频 acdcdig-dsm |
| GPIO2_A0～A5 | 111～116 | SPI NAND（FSPI），根文件系统在上面 |
| GPIO2_B0～C0 | 132～137、139、141、142 | 网口 RMII0（ethernet@ff4c8000） |
| GPIO3_A0～A5 | 102、103、105、107～109 | TF 卡（mmc@ff480000） |
| GPIO4_B0、B1 | 9、10 | 启动脚（对照表写“启动项必要”）。B1 还是 Linux adc-keys 的按键 |

### 9.3 坑

1. **gpio2 pin4/5 是 NAND 的数据线。** GPIO2_A4/A5 就是 FSPI_D2/D3（SoM 116、113 脚）。
   对照表写着这几根脚“与核心板SPI NAND FLASH引脚复用，只能在EMMC版本核心板使用”，这块板是 NAND 版。
   板上 DTB 里 NAND 按四线读（spi-rx-bus-width 4）。
   Zephyr 的 `gpio_pin_configure()` 会把脚的 IOMUX 改成 GPIO，顺带改上下拉。
   所以用老的 overlay，不接跳线，光跑 gpio_basic_api 就会把 NAND 的两根数据线切走，Linux 读根文件系统会出错。
   已改到 GPIO3_A6/A7。
2. **debugfs 看不出 NAND 的脚被占。** FSPI 节点没有 pinctrl，这几根脚是启动时就设好的。
   `pinmux-pins` 里 GPIO2_A0～A5 显示 UNCLAIMED，其实在用。要看 IOMUX 寄存器，见 9.5。
3. **Zephyr 的 GPIO 驱动不管 Linux 在用什么。** 对哪根脚调了 `gpio_pin_configure()`，哪根脚就被拿走。
   只能靠 overlay 里的 `gpio-reserved-ranges` 挡住。新的几个 overlay 都只放出要用的一两根脚。
   驱动初始化不动整组寄存器，配置时也只改那一根脚，所以和 Linux 共用一个 bank 没问题。
4. **GPIO 中断要用第 3 组。** 每个 GPIO bank 有 4 组中断输出，Linux 的 GPIO 驱动只用第 0 组。
   板上 DTB 的 `amp-irqs` 把每个 bank 的第 3 组（GIC 35、39、43、47、51）交给 CPU2。
   Zephyr 默认用第 0 组，会和 Linux 抢同一个中断。用 GPIO 中断必须设 `rockchip,irq-group = <3>`。
   gpio_basic_api 要测中断回调，新 overlay 已经设成 3。gpio_power_cycle 写的是 2，它不用中断，没影响。
5. **GPIO0_A3 是背光。** 以前的 GPIO 片选编译检查用的就是它，已改到 GPIO0_B5。
6. **CAN0 占着 SPI 的两根脚。** GPIO0_B6、C0 在底板上叫 SPI1_CSN0、SPI0_CLK。
   CAN0 在用时，SPI1 只能用 CSN1，SPI0 不能走引脚。`external-pins.overlay` 要关掉 CAN0，所以只做编译检查。
7. **别回到老的 CAN0 接法。** 以前试过 RM_IO19/20（GPIO0_C3/C4）。C4 是网口 PHY 复位。
8. **一个 RM_IO 功能号只能给一根脚。** UART1 的 1、2 由 AMP 节点设，和 Zephyr 设的是同一组脚，没冲突。
   此外 Linux 还选了 15、16（I2C0）、19、20（I2C2）、30、31、32（PWM0_CH0～2）。
   Zephyr 不要把这些功能号再选到别的脚上。

### 9.4 建议，还没做

在板级 DTS（`ok3506b_s12_amp_uart1.dts`）里给每个 gpio 节点默认加上 `gpio-reserved-ranges`，挡住 9.2 里 Linux 的脚。
这样应用的 overlay 忘了写，也抢不到 Linux 的脚。现在是每个 overlay 自己写。要做就说一声。

### 9.5 上板时顺手查

Linux 下跑，任何一版镜像都行：

```sh
mount | grep -q debugfs || mount -t debugfs none /sys/kernel/debug
grep -v UNCLAIMED /sys/kernel/debug/pinctrl/*/pinmux-pins
cat /sys/kernel/debug/gpio
printf 'gpio2a_sel1=0x%x\n' $(( $(devmem 0xff4d8044 32) & 0xffff ))
printf 'gpio3a_sel1=0x%x\n' $(( $(devmem 0xff4d8064 32) & 0xffff ))
dmesg | grep -i -E 'nand|ubi|ecc'
```

期望：

- `pinmux-pins` 和 `gpio` 里没有 GPIO0_B5、B6、C0 和 GPIO3_A6、A7。
  内核按 bank×32+脚号编号，这几根是 13、14、16、102、103，在 `pinmux-pins` 里叫 `gpio0-13`、`gpio3-6` 这样。
  有的话把那几行发给我。
- `gpio2a_sel1=0x11`：GPIO2_A4/A5 是功能 1（FSPI_D2/D3）。不是 0x11 就是 NAND 的数据线被切走了。
- `gpio3a_sel1=0x11`：A4/A5 是功能 1（TF 卡），A6/A7 是 0（GPIO）。
- dmesg 里只有 NAND 的识别信息，没有 error、ECC、UBI 报错。

以前用老的 overlay（gpio2 pin4/5）跑过 gpio_basic_api 的话，重点看最后一条。
那一次 NAND 的 D2/D3 被切走，读出来的数据可能是错的。跑的时候有没有写过 NAND，也告诉我。
