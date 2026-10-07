# RK3506 Zephyr BSP 上游化差距报告

> 2026-05 的历史评估，以下目录和验证结果对应当时的工程。
> 当前结构和待办见 [STATUS.md](../../../../docs/rk3506/STATUS.md) 与
> [BSP-CHECKLIST.md](../../../../docs/rk3506/BSP-CHECKLIST.md)。

日期：2026-05-16

## 1. 范围

本文记录当前 `zephyr-rk3506` 这份 RK3506 Zephyr BSP 距离“符合 Zephyr
官方结构、未来可考虑提交 upstream”的状态还差什么。

当前活动树：

```text
zephyr-rk3506/
```

当前主板平台：

```text
ok3506b_s12_amp_uart1
```

当前目标不是“马上 upstream”，而是先把本地 BSP 做到：

- 目录结构接近 Zephyr 官方习惯；
- sample/test 边界清楚；
- SoC/board/DTS/Kconfig/CMake 分层合理；
- 特殊的 RK3506 AMP 约束有明确文档说明；
- 后续维护时不再依赖口口相传。

## 2. 当前已经对齐的内容

### 2.1 模块入口

已使用 Zephyr out-of-tree module 方式：

```text
zephyr-rk3506/zephyr/module.yml
```

它声明：

```text
board_root: .
soc_root: .
dts_root: .
```

这意味着 `zephyr-rk3506` 可以作为一个 Zephyr module 被 `west` / CMake /
Twister 识别，而不是只靠 SDK 私有脚本硬塞路径。

### 2.2 board 目录

当前 board 路径：

```text
zephyr-rk3506/boards/forlinx/ok3506b_s12/
```

当前 board DTS：

```text
zephyr-rk3506/boards/forlinx/ok3506b_s12/ok3506b_s12_amp_uart1.dts
```

当前 board platform：

```text
ok3506b_s12_amp_uart1
```

说明：

- `forlinx` 是厂商目录；
- `ok3506b_s12` 是板级目录；
- `ok3506b_s12_amp_uart1` 表示当前这个 Zephyr 镜像是 OK3506B-S12 的
  AMP CPU2 UART1 固件目标；
- `rk3506` 是 `list_boards` 报告的 HWMv2 qualifier。当前 Zephyr v4.4
  Twister 平台名仍使用未带 qualifier 的 `ok3506b_s12_amp_uart1`。

### 2.3 SoC 目录

当前 SoC 路径：

```text
zephyr-rk3506/soc/rockchip/rk3506/
```

关键文件：

```text
Kconfig
Kconfig.defconfig
Kconfig.soc
CMakeLists.txt
soc.c
soc.h
soc.yml
rk3506_amp_layout.h
rk3506_rpmsg_platform.c
rk3506_rpmsg_platform.h
```

当前公共 Kconfig 功能符号已经使用 SoC 命名空间：

```text
SOC_RK3506_*
```

例如：

```text
CONFIG_SOC_RK3506_RPMSG_LITE
CONFIG_SOC_RK3506_RPMSG_MBOX_IRQ
CONFIG_SOC_RK3506_PRIVATE_ICACHE
CONFIG_SOC_RK3506_PRIVATE_DCACHE
CONFIG_SOC_RK3506_AMP_DIAGNOSTICS
```

内部 C 常量仍保留 `RK3506_*`，例如地址、mailbox magic、link id、endpoint
等。这是合理的：它们是芯片/协议常量，不是 Kconfig 公共配置符号。

### 2.4 DTS 和 binding

当前 RK3506 SoC include：

```text
zephyr-rk3506/dts/arm/rockchip/rk3506.dtsi
```

当前 mailbox binding：

```text
zephyr-rk3506/dts/bindings/mbox/rockchip,rk3506-mailbox.yaml
```

当前本地 vendor prefix：

```text
zephyr-rk3506/dts/bindings/vendor-prefixes.txt
```

说明：

- upstream Zephyr 主树里目前没有 `forlinx` prefix；
- 本地 out-of-tree module 可以先提供自己的 `vendor-prefixes.txt`；
- 真正 upstream 时，需要确认 Zephyr 社区接受的厂商 spelling。

### 2.5 sample/test 边界

生产 sample：

```text
zephyr-rk3506/samples/subsys/ipc/rpmsg/rk3506_pingpong/
```

当前 sample 源码只保留：

```text
src/main.c
src/rk3506_rpmsg_demo.c
src/rk3506_rpmsg_demo.h
```

它只负责：

- 启动 Zephyr CPU2；
- 初始化 RPMsg-Lite remote；
- announce `rpmsg-ap3-ch0`；
- 收到 Linux `ping:<seq>` 后回复 `pong:<seq>`。

验证 test：

```text
zephyr-rk3506/tests/ztest/rk3506_amp_cache_stress/
```

test 保留：

```text
rk3506_amp_probe.c/h
rk3506_cache_thrash.c/h
```

也就是说：

- sample 不再携带 probe/thrash 诊断代码；
- probe/thrash 只属于验证目标；
- 生产镜像和验证镜像边界清楚。

## 3. 已经在硬件上验证过的运行基线

当前 RK3506 Zephyr AMP 基线已经过实板验证：

- Zephyr 能在 RK3506 CPU2 从 `0x03e00000` 启动；
- UART1 console 可用；
- Zephyr shell 可用；
- Linux 侧能看到 RPMsg channel：

```text
rpmsg-ap3-ch0
```

- Linux 用户态工具：

```sh
/userdata/rk3506_rpmsg_char_ping
```

能收到：

```text
rx len=6 data="pong:1"
```

- Linux -> Zephyr 通知路径使用 mailbox2 IRQ 176；
- IRQ 176 的 GIC target mask 为 `0x4`；
- IRQ 176 使用 Group0；
- CPU2 private I-cache 可开启；
- CPU2 private D-cache 可开启；
- `0x03b00000` AMP shmem 和 `0x03c00000` RPMsg window 已按 strongly ordered
  处理；
- RPMsg/cache 长时压力测试已经通过。

## 4. 当前刻意保留的 out-of-tree 例外

这些不是 bug，而是当前 SDK/板级现实约束。它们可以在本地 BSP 中保留，但如果
要 upstream，需要进一步拆分或设计。

### 4.1 镜像打包仍依赖 Rockchip SDK

当前 AMP 固件打包依赖：

```text
./build.sh amp
./build.sh updateimg
```

产物通过 Rockchip SDK 生成 `amp.img` / `update.img`。

这不是 Zephyr 官方 flash runner / sysbuild 路径。

本地可以接受；upstream 前必须明确：

- 只提交 BSP，不提交 Rockchip SDK 打包逻辑；
- 或者添加 Zephyr runner/sysbuild；
- 或者在 board 文档里声明该目标不能通过通用 Zephyr flash 流程烧录。

### 4.2 Linux 侧 RPMsg host 是 Rockchip 私有实现

Linux host 侧由 Rockchip `rockchip-rpmsg` 驱动负责。

关键运行信息：

```text
rpmsg@3c00000
vring0 0x03c00000
vring1 0x03c08000
rpmsg-dma 0x03d00000
```

Zephyr 侧是 remote endpoint，不是完整 host。

因此当前 sample/test 的运行必须依赖 OK3506 Linux 环境，不能在普通 Twister
自动测试里真实跑通。

### 4.3 RPMsg-Lite 使用 RK3506 platform glue

当前 RPMsg-Lite 的平台层在：

```text
soc/rockchip/rk3506/rk3506_rpmsg_platform.c
```

它负责：

- mailbox register 访问；
- mailbox2 IRQ 176 处理；
- GIC target routing；
- RPMsg-Lite env 回调；
- Linux/RTT 兼容的通知语义。

这不是 Zephyr 通用 IPC service backend。

本地 BSP 可以这样做，但 upstream 前要决定是否需要改成 Zephyr IPC service。

### 4.4 mailbox 还不是 Zephyr MBOX driver

当前 mailbox 只是 RK3506 RPMsg platform glue 的一部分。

它还不是：

```text
drivers/mbox/
```

下的通用 Zephyr mailbox driver。

这点需要保留为明确差距，不能假装已经完成 Zephyr driver 化。

## 5. upstream 前必须决策的问题

### 5.1 Mailbox 模型

问题：

```text
RK3506 mailbox 到底应该只是 RPMsg platform glue，
还是应该成为 Zephyr MBOX driver？
```

当前建议：

- 短期：保持 SoC platform glue；
- 原因：当前 mailbox 用法和 Linux AMP/RPMsg host 强绑定；
- upstream 前：重新评估是否拆到 `drivers/mbox/`。

### 5.2 IPC 模型

问题：

```text
当前 RPMsg-Lite remote endpoint 是否应该改成 Zephyr IPC service？
```

当前建议：

- 短期：保持 sample + SoC RPMsg-Lite glue；
- 原因：Linux host ABI 固定，先不要破坏已验证链路；
- upstream 前：确认 Zephyr 社区是否接受这种 sample 方式，或要求 IPC service。

### 5.3 CPU2 建模

问题：

```text
CPU2 AMP 目标是否应该在 soc.yml 里建模成 CPU cluster / variant？
```

当前状态：

```text
ok3506b_s12_amp_uart1
```

这个名字把“AMP CPU2 UART1 固件用途”放进了 board platform。

短期可以接受，因为它准确描述了当前固件用途。

upstream 前需要判断：

- 是否应该建一个更正式的 CPU2 variant；
- 是否应该把 AMP CPU2 作为 board qualifier；
- 是否应该拆出更通用的 `ok3506b_s12/rk3506` 和应用 overlay。

### 5.4 Build/flash 集成

问题：

```text
Rockchip SDK update.img 流程是否可以进入 upstream？
```

答案基本是否定的。

upstream 前必须做到至少一种：

1. 只提交 Zephyr BSP，不提交 SDK 打包；
2. 添加 Zephyr 原生 runner；
3. 文档声明 flash 不支持通用 Zephyr 流程，需外部 Rockchip 工具。

### 5.5 Vendor prefix

问题：

```text
forlinx 这个 vendor prefix 是否能被 Zephyr upstream 接受？
```

当前本地文件：

```text
dts/bindings/vendor-prefixes.txt
```

upstream 前需要：

- 确认厂商官方英文名；
- 确认 prefix 是否用 `forlinx`；
- 提交到 Zephyr 主树 vendor prefix 列表。

### 5.6 Board 文档

upstream 前必须补 Zephyr board 文档，至少说明：

- 板名：Forlinx OK3506B-S12；
- platform：`ok3506b_s12_amp_uart1`；qualifier：`rk3506`；
- 这是 AMP CPU2 固件；
- Linux host 是必须条件；
- UART1 是 Zephyr console；
- RPMsg channel 是 `rpmsg-ap3-ch0`；
- 当前烧录依赖 Rockchip SDK `update.img`；
- 普通 `west flash` 暂不支持，除非后续补 runner。

## 6. 当前验证结果

### 6.1 layout checker

命令：

```sh
/home/kuro/.codex/skills/zephyr-bsp-compliance/scripts/check_rk3506_zephyr_layout.sh zephyr-rk3506
```

结果：

```text
pass
```

### 6.2 list_boards

命令：

```sh
ZEPHYR_BASE=/home/kuro/zephyr-work/zephyr \
/home/kuro/venv-zephyr-4.4/bin/python3 \
/home/kuro/zephyr-work/zephyr/scripts/list_boards.py \
  --board-root zephyr-rk3506 \
  --soc-root zephyr-rk3506 \
  --board ok3506b_s12_amp_uart1 \
  --cmakeformat '{NAME}|{QUALIFIERS}|{HWM}|{DIR}|{VENDOR}'
```

结果：

```text
NAME;ok3506b_s12_amp_uart1|QUALIFIERS;rk3506|HWM;v2|DIR;.../zephyr-rk3506/boards/forlinx/ok3506b_s12|VENDOR;forlinx
```

### 6.3 list_hardware

命令：

```sh
ZEPHYR_BASE=/home/kuro/zephyr-work/zephyr \
/home/kuro/venv-zephyr-4.4/bin/python3 \
/home/kuro/zephyr-work/zephyr/scripts/list_hardware.py \
  --soc-root zephyr-rk3506 \
  --socs \
  --format '{type}|{name}|{dir}'
```

结果：

```text
series|rk3506|.../zephyr-rk3506/soc/rockchip/rk3506
soc|rk3506|.../zephyr-rk3506/soc/rockchip/rk3506
```

### 6.4 Twister sample build-only

命令：

```sh
ZEPHYR_BASE=/home/kuro/zephyr-work/zephyr \
ZEPHYR_EXTRA_MODULES=/home/kuro/rk3506/OK3506B-S12_Linux6.1.99/SDK/OK3506_Linux_Source/zephyr-rk3506 \
ZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
CROSS_COMPILE=/home/kuro/rk3506/OK3506B-S12_Linux6.1.99/SDK/OK3506_Linux_Source/prebuilts/gcc/linux-x86/arm/gcc-arm-none-eabi-10-2020-q4-major-x86_64-linux/bin/arm-none-eabi- \
/home/kuro/venv-zephyr-4.4/bin/python3 \
/home/kuro/zephyr-work/zephyr/scripts/twister \
  -T zephyr-rk3506/samples/subsys/ipc/rpmsg/rk3506_pingpong \
  -p ok3506b_s12_amp_uart1 \
  --build-only \
  --inline-logs \
  -A zephyr-rk3506/boards \
  --force-toolchain \
  --outdir output/twister-phase7_2-sample
```

结果：

```text
1 built, 0 failed, 0 errored, no warnings
```

### 6.5 Twister cache-stress build-only

命令：

```sh
ZEPHYR_BASE=/home/kuro/zephyr-work/zephyr \
ZEPHYR_EXTRA_MODULES=/home/kuro/rk3506/OK3506B-S12_Linux6.1.99/SDK/OK3506_Linux_Source/zephyr-rk3506 \
ZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
CROSS_COMPILE=/home/kuro/rk3506/OK3506B-S12_Linux6.1.99/SDK/OK3506_Linux_Source/prebuilts/gcc/linux-x86/arm/gcc-arm-none-eabi-10-2020-q4-major-x86_64-linux/bin/arm-none-eabi- \
/home/kuro/venv-zephyr-4.4/bin/python3 \
/home/kuro/zephyr-work/zephyr/scripts/twister \
  -T zephyr-rk3506/tests/ztest/rk3506_amp_cache_stress \
  -p ok3506b_s12_amp_uart1 \
  --build-only \
  --inline-logs \
  -A zephyr-rk3506/boards \
  --force-toolchain \
  --outdir output/twister-phase7_2-cache
```

结果：

```text
1 built, 0 failed, 0 errored, no warnings
```

### 6.6 SDK AMP build

命令：

```sh
Python3_EXECUTABLE=/home/kuro/venv-zephyr-4.4/bin/python3 ./build.sh amp
```

结果：

```text
pass
```

产物：

```text
output/zephyr-build/amp2/zephyr/zephyr.bin
zephyr-rk3506/out/zephyr.bin
output/firmware/amp.img
```

SHA256：

```text
8d42ee618abe46caf27d3b5b333af7482d09f97a2f1d2b2a5323bf636fee528a  output/zephyr-build/amp2/zephyr/zephyr.bin
8d42ee618abe46caf27d3b5b333af7482d09f97a2f1d2b2a5323bf636fee528a  zephyr-rk3506/out/zephyr.bin
f322400e9c38a2ae084639d22cbd9f450ea872c28e7e07f5569a5d7ce16083ee  output/firmware/amp.img
```

## 7. 当前结论

当前 BSP 已经进入“本地结构基本可维护”的状态。

已经完成：

- Zephyr module 化；
- board/soc/dts/binding 基本按官方目录放置；
- production sample 和 validation test 分离；
- Kconfig 命名收敛到 `SOC_RK3506_*`；
- RPMsg/mailbox 例外有文档说明；
- 硬件运行链路已经验证；
- build-only Twister 能过；
- SDK AMP build 能过。

仍未完成、但已明确归档：

- mailbox 是否上升为 Zephyr MBOX driver；
- RPMsg 是否上升为 Zephyr IPC service backend；
- CPU2 是否需要更正式的 SoC/cluster 建模；
- Rockchip SDK `update.img` 与 Zephyr upstream build/flash 的边界；
- `forlinx` vendor prefix upstream 接受问题；
- Zephyr board 文档页。

下一阶段如果继续推进 upstream 质量，建议优先做：

1. board 文档页；
2. mailbox driver 可行性调研；
3. IPC service/RPMsg-Lite 取舍设计；
4. CPU2 target/qualifier 方案定稿。
