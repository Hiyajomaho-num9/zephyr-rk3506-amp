# RK3506 AMP 启动链与 Zephyr 替换可行性总结

- 归档日期：2026-04-21
- SDK 根目录：`/home/kuro/rk3506/OK3506B-S12_Linux6.1.99/SDK/OK3506_Linux_Source`
- 总结目的：沉淀当前对 RK3506 AMP 启动链、U-Boot 代码路径、以及用 Zephyr 替换 RT-Thread 的可行性判断，供后续继续实施“完整替换 RT-Thread AMP 方案”使用。

## 1. 本次总结使用的上下文来源

已读取来源：

- `/home/kuro/rk3506_task_summary.md`
- `amp-uboot-bringup-archive.md`
- 当前 SDK 实际文件树
- Luckyfox U-Boot 源码树：`/home/kuro/rk3506/Luckfox/u-boot`
- Zephyr 官方仓库（临时浅克隆到 `/tmp/zephyr-rk3506-check`）

本总结以“当前代码树 + 历史归档 + 实际验证结论”为主。

## 2. 当前已经确认成立的事实

### 2.1 AMP 问题的真实根因

此前板子上真正的问题不在 Linux DTS，也不在 `amp.img` 内容，而在 U-Boot 侧没有把 AMP 启动链打通。

更换成带 Rockchip AMP FIT loadables 启动逻辑的 U-Boot 后，已确认：

- U-Boot 成功加载 `amp2`
- CPU2 成功拉起
- RTOS 成功启动
- Linux 侧 `rockchip-rpmsg` 正常 online

### 2.2 当前 AMP 产品配置

顶层产品配置：

- `device/rockchip/.chips/ok3506/OK3506-S-MINI_amp_nand_defconfig`

关键字段：

- `RK_AMP=y`
- `RK_AMP_FIT_ITS="amp_linux.its"`
- `RK_UBOOT_CFG_FRAGMENTS="rk-amp"`
- `RK_KERNEL_DTS_NAME="OK3506-S-MINI_amp_nand"`
- `RK_PARAMETER="parameter-mini-amp-nand.txt"`
- `RK_USE_FIT_IMG=y`

### 2.3 当前 RTOS AMP 固件入口

AMP FIT 描述：

- `device/rockchip/.chips/ok3506/amp_linux.its`

关键定义：

- `amp2` 的数据体是 `rtt2.bin`
- `cpu = <0xf02>`
- `load = <0x03e00000>`
- `rtt_config = "board/evb1/ok3506_amp_uart1_defconfig"`

这说明当前 U-Boot 侧真正做的事是：

- 从 `amp` 分区读取 `amp.img`
- 在 FIT `conf` 里找到 `loadables = "amp2"`
- 将 `amp2` 数据装载到 `0x03e00000`
- 拉起 `0xf02`

### 2.4 当前 Linux 启动镜像入口

Linux FIT 描述：

- `device/rockchip/.chips/ok3506/zboot.its`

内容为：

- `kernel`
- `fdt`
- `resource`

说明当前 `boot.img` 是 FIT，而不是简单的裸 kernel。

## 3. 已陪读的 U-Boot 启动链源码摘要

本阶段已经结合 Luckyfox 的 U-Boot 源码，陪读过 3 个最关键的启动链文件：

- `common/spl/spl.c`
- `common/spl/spl_fit.c`
- `arch/arm/mach-rockchip/board.c`

这 3 个文件的职责边界如下。

### 3.1 `common/spl/spl.c` 的职责

定位：

- SPL 阶段总控

第一轮精读的关键函数：

- `spl_load_image()`
- `boot_from_devices()`
- `board_init_r()`
- `spl_cleanup_before_jump()`

核心结论：

1. `spl.c` 自己不是 FIT 解包器，而是 SPL 的调度器。
2. 它先根据 `board_boot_order()` 产生 boot device 列表。
3. 然后在 `boot_from_devices()` 里逐个尝试 loader。
4. 成功后根据 `spl_image.os` 决定最终跳转目标：
   - `IH_OS_U_BOOT`
   - `IH_OS_ARM_TRUSTED_FIRMWARE`
   - `IH_OS_OP_TEE`
   - `IH_OS_LINUX`

与实际日志的对应关系：

- `Trying to boot from MTD1`
  - 来自 `boot_from_devices()`
- `Jumping to U-Boot(...) via OP-TEE(...)`
  - 来自 `board_init_r()` 中对 `spl_image.os == IH_OS_OP_TEE` 的分支处理

换句话说：

> `spl.c` 回答的是“SPL 最终跳给谁”，而不是“镜像内部怎么解”。

### 3.2 `common/spl/spl_fit.c` 的职责

定位：

- SPL 阶段的 FIT 解包器

第一轮精读的关键函数：

- `spl_fit_get_image_name()`
- `spl_fit_get_image_node()`
- `spl_load_fit_image()`
- `spl_internal_load_simple_fit()`
- `spl_fit_append_fdt()`

核心结论：

1. `spl_fit.c` 会先找到 FIT 的当前配置节点。
2. 再按顺序选择主镜像：
   - `firmware`
   - `kernel`
   - `loadables[0]`
3. 然后调用 `spl_load_fit_image()` 把主镜像装入内存，并把结果写进 `spl_image`。
4. 接着继续装剩余的 `loadables`，并补充：
   - `entry_point_os`
   - `entry_point_bl32`
   - `entry_point_bl33`
5. `spl_image.os` 也是在这个文件里从 FIT 主镜像属性读出来的。

这意味着：

> `spl.c` 后面之所以会走 `IH_OS_OP_TEE` 分支，并不是它自己猜出来的，而是 `spl_fit.c` 先把 `spl_image.os` 填成了 `IH_OS_OP_TEE`。

### 3.3 `arch/arm/mach-rockchip/board.c` 的职责

定位：

- U-Boot proper 阶段的板级启动策略层

第一轮精读的关键函数：

- `env_fixup()`
- `cmdline_handle()`
- `board_late_init()`
- `bootm_board_start()`
- `bootm_image_populate_dtb()`
- `board_do_bootm()`

核心结论：

1. `board_late_init()` 是 U-Boot proper 阶段最关键的板级入口。
2. 在这里会先做：
   - `env_fixup()`
   - `cmdline_handle()`
   - `soc_clk_dump()`
3. 然后在 `CONFIG_AMP` 打开时调用：
   - `amp_cpus_on()`
4. 说明 RTOS/AMP 固件是在 U-Boot proper 阶段启动，而不是 Linux 阶段启动。
5. `board_do_bootm()` 会拦截 `bootm <addr>`：
   - 如果发现镜像是 FIT
   - 就转成执行 `boot_fit <addr>`

这意味着当前 Linux 启动链不是简单的：

```text
bootm -> kernel
```

而是：

```text
bootm
-> board_do_bootm()
-> boot_fit
-> do_bootm_states()
-> kernel
```

### 3.4 读完这 3 个文件后的阶段性结论

目前已经能用源码解释这几件事：

1. `Trying to boot from ...` 是 SPL 在选启动设备。
2. `via OP-TEE` 是 SPL 根据 `spl_image.os` 走出来的路径。
3. U-Boot proper 会在 `board_late_init()` 中拉起 AMP。
4. Linux FIT 镜像会被 `board_do_bootm()` 转交给 `boot_fit`。

所以就 U-Boot 启动链而言，当前已经建立起了下面这条最重要的主线：

```text
common/spl/spl.c
-> common/spl/spl_fit.c
-> OP-TEE
-> U-Boot proper: arch/arm/mach-rockchip/board.c
-> AMP bring-up
-> boot_fit
-> Linux
```

## 4. U-Boot proper 到底怎样把 `amp2 -> rtt2.bin -> CPU2` 拉起来

这条链分成“构建时”和“运行时”。

### 3.1 构建时

#### 3.1.1 `mk-amp.sh` 先编 RT-Thread

文件：

- `device/rockchip/common/scripts/mk-amp.sh`

核心逻辑：

- 读取 `amp_linux.its`
- 解析 `compile {}` 中的 `sys = "rtt"` 和 `rtt_config`
- 调用 `build_rtthread(...)`
- 产出 `rtt2.bin`

#### 3.1.2 `mk-amp.sh` 再把 `rtt2.bin` 打进 `amp.img`

同一个脚本最后会：

- 生成临时 `amp.its`
- 去掉 `compile {}` 和 `share {}` 等构建期信息
- 调用 `mkimage -f amp.its ... amp.img`

结果：

- `amp.img` 被烧进 `amp` 分区

### 3.2 运行时

#### 3.2.1 U-Boot proper 在 `board_late_init()` 中启动 AMP

文件：

- `arch/arm/mach-rockchip/board.c`

关键路径：

- `board_late_init()`
- `env_fixup()`
- `cmdline_handle()`
- `amp_cpus_on()`

结论：

- AMP 不是 Linux 启动后拉起的
- 而是 U-Boot proper 晚期初始化阶段拉起的

#### 3.2.2 `amp_cpus_on()` 直接读取 `amp` 分区

文件：

- `drivers/cpu/rockchip_amp.c`

关键定义：

- `#define AMP_PART "amp"`

关键流程：

- `rockchip_get_bootdev()`
- `part_get_info_by_name(..., "amp", ...)`
- 读取 FIT 头
- 检查 `fdt_check_header()`
- 读取完整 `amp.img`

#### 3.2.3 通过 `boot_get_loadable()` 加载 `amp2`

文件：

- `drivers/cpu/rockchip_amp.c`
- `common/image.c`

关键行为：

- 强制使用配置名 `conf`
- 调 `boot_get_loadable(...)`
- 遍历 `loadables`
- 发现 `amp2`
- 把 `amp2` 子镜像加载到 ITS 里声明的 `load` 地址

#### 3.2.4 `brought_up_amp()` 读取 `cpu/load/hyp/thumb`

文件：

- `drivers/cpu/rockchip_amp.c`

读取内容：

- `cpu`
- `load`
- `hyp`
- `thumb`
- `boot-on`

非 Linux 固件路径会：

- 用 `sysmem_alloc_base_by_name(...)` 占住 `load` 对应内存
- 防止后续 Linux 启动覆盖此区域

#### 3.2.5 `smc_cpu_on()` 通过 SMC + PSCI 真的启动 CPU2

文件：

- `drivers/cpu/rockchip_amp.c`

核心动作：

- `sip_smc_amp_cfg(...)` 设置 AMP 处理器状态
- `psci_cpu_on(cpu, entry)` 拉起目标 CPU

这就是日志：

```text
AMP: Brought up cpu[f02] with state 0x10, entry 0x03e00000 ...OK
```

的直接来源。

### 3.3 这一段的工程结论

U-Boot proper 并不关心 `amp2` 里跑的是 RT-Thread 还是别的系统。

它真正依赖的是：

- `amp.img` 是合法 FIT
- `conf` 下有 `loadables`
- `amp2` 有合法的 `cpu/load/...` 属性
- 数据体是一个可以从入口直接执行的固件二进制

因此从 U-Boot AMP loader 视角：

- `rtt2.bin` 可以被别的二进制替换
- 理论上 `zephyr.bin` 也可以走同一条链

## 5. U-Boot proper 怎样继续启动 Linux

在 `arch/arm/mach-rockchip/board.c` 中：

- `board_do_bootm()` 会先判断镜像格式
- 如果是 FIT，转为执行 `boot_fit <addr>`

而：

- `cmd/bootfit.c` 负责输出 `## Booting FIT Image ...`
- `arch/arm/mach-rockchip/fit.c` 默认从 `boot` 分区读 FIT
- 然后进入通用 `do_bootm_states()`

也就是说当前 Linux 链路是：

```text
bootm
-> board_do_bootm()
-> boot_fit
-> fit_image_load_bootables()
-> do_bootm_states()
-> kernel + fdt + resource
```

## 6. 用 Zephyr 替换 RT-Thread 的可行性评估

## 6.1 可行性结论

### 低目标：只让 CPU2 跑 Zephyr 并打印 UART

可行性：高<br>
工作量：中

原因：

- U-Boot AMP loader 对固件种类不敏感
- 只要最终产物是一个可执行的 `zephyr.bin`
- 并能放入 `amp2` 的 FIT 子镜像
- 就有机会直接沿用现有 U-Boot AMP 启动链

### 中目标：Zephyr 跑起来，并保留基础共享内存

可行性：中高<br>
工作量：中高

### 高目标：完整替换 RT-Thread，并兼容现有 Linux `rockchip-rpmsg`

可行性：中<br>
工作量：高

原因不是 U-Boot，而是 Zephyr 自身在 RK3506 平台侧的缺口。

## 6.2 我对 Zephyr 官方仓库的实际检查结果

本地检查使用：

- 仓库：`https://github.com/zephyrproject-rtos/zephyr`
- 本地临时路径：`/tmp/zephyr-rk3506-check`

### 已确认有利条件

#### 1）Zephyr 架构层支持 Cortex-A7 / ARMv7-A

文件：

- `arch/arm/core/cortex_a_r/Kconfig`

可见：

- `config CPU_CORTEX_A7`
- `select CPU_AARCH32_CORTEX_A`
- `select ARMV7_A`

说明：

- Zephyr 架构本身支持 32 位 Cortex-A7

#### 2）Zephyr 已有 Cortex-A 板子由 U-Boot 启动的用法

官方文档中，NXP 一些 A53/A55 板子明确支持：

- 由 U-Boot `go`
- 或 U-Boot `cpu` 命令

将 Zephyr 装入某个 Cortex-A 核运行。

这说明：

- “Zephyr 被外部 bootloader 拉起”这个模式是官方允许的

#### 3）Zephyr 具有 OpenAMP / RPMsg 子系统

仓库中可见：

- `subsys/ipc/open-amp/`
- `subsys/ipc/rpmsg_service/`
- `subsys/ipc/ipc_service/lib/ipc_rpmsg.c`

说明：

- Zephyr 并不是没有 IPC / RPMsg / OpenAMP 能力

### 已确认的不利条件

#### 1）Zephyr 当前没有 RK3506 SoC / Board 支持

当前 `soc/rockchip/soc.yml` 中只有：

- `rk3399`
- `rk3568`
- `rk3588`
- `rk3588s`

没有 `rk3506`。

#### 2）Rockchip 支持当前主要在 ARM64 RK35/<!--  -->RK3399

当前可见 Rockchip DTS 基本都在：

- `dts/arm64/rockchip/`

没有现成：

- `rk3506`
- `arm32/rockchip/rk3506`

对应 BSP。

#### 3）没有现成 Rockchip mailbox / RK3506 RPMsg transport

当前没有直接可复用的：

- Rockchip mailbox 驱动
- 与现有 Linux `rockchip-rpmsg` 配套的 RK3506 backend

这意味着：

- 如果只做 `UART hello`，问题不大
- 如果要完整兼容现有 Linux RPMsg，平台适配工作量会明显上升

## 6.3 工程上的真实判断

### 可以先做的

最现实的第一阶段目标：

1. 保持现有 U-Boot AMP 启动链不变
2. 把 `amp2` 的数据体从 `rtt2.bin` 改为 `zephyr.bin`
3. 让 CPU2 在 `0x03e00000` 跑起来
4. UART1 输出

这一步主要是验证：

- U-Boot AMP loader 对 Zephyr 二进制同样适用
- RK3506 上 Zephyr 最小 BSP 可以跑通

### 不建议直接一步做满的

不建议一开始就同时做：

- Zephyr BSP
- UART
- GIC
- timer
- mailbox
- RPMsg
- Linux 端协同

更合理的顺序是：

1. 先 bring-up Zephyr 到 CPU2
2. 再补中断/定时器/UART 稳定性
3. 最后再做 OpenAMP / RPMsg 对接

## 7. 推荐的实施路线

### 第一阶段：最小可运行目标

目标：

- `amp2 -> zephyr.bin`
- CPU2 成功执行
- UART1 输出 Zephyr hello

阶段产出：

- 新的 Zephyr board/SoC 最小 port
- 能替换 `rtt2.bin` 的 `zephyr.bin`

### 第二阶段：最小板级能力

补齐：

- UART1
- GIC
- arch timer
- 基本 memory map
- 入口链接地址 `0x03e00000`

### 第三阶段：Linux 通信能力

补齐：

- mailbox
- shared-memory
- vrings
- OpenAMP / RPMsg backend

### 第四阶段：完整替换 RT-Thread AMP 方案

目标：

- 用 Zephyr 替代当前 RT-Thread AMP 固件
- 保留现有 U-Boot AMP 启动方式
- 最终与 Linux 侧 RPMsg 对接

## 8. 当前阶段结论

### 结论 1

从 U-Boot proper 的代码实现看，`amp2 -> rtt2.bin -> CPU2` 并不依赖 RT-Thread，只依赖：

- FIT 格式
- `cpu/load` 元数据
- 可执行固件二进制

### 结论 2

因此从 U-Boot AMP loader 视角，Zephyr 替换 RT-Thread 是成立的。

### 结论 3

真正的难点不在 U-Boot，而在：

- RK3506 在 Zephyr 中没有现成 BSP
- Rockchip mailbox / RPMsg transport 没现成实现

### 结论 4

最合理路线不是“直接完整替换”，而是：

- 先把 Zephyr 当作新的 `amp2` 固件 bring-up
- 再逐步替换 RT-Thread 的平台能力和通信能力

## 9. 作为后续任务的建议入口

如果下一步要正式推进“完整替换 RT-Thread AMP 方案”，建议从下面两个方向同时建任务：

### 方向 A：Zephyr 最小 bring-up

- 目标：CPU2 + UART1 + hello
- 不碰 RPMsg

### 方向 B：现有 AMP 资源建模

- 把当前 RT-Thread 方案中这些信息整理成 Zephyr 迁移输入：
  - memory map
  - reserved-memory
  - UART1 pinmux
  - clock
  - GIC irq route
  - mailbox / link-id

这样后面做 Zephyr BSP 时，不用再从零倒推。
