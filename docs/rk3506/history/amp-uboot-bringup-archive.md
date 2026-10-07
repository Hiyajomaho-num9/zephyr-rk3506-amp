# RK3506 AMP U-Boot 启动问题归档

- 归档日期：2026-04-18
- 适用范围：`OK3506-S-MINI` AMP NAND 启动链
- SDK 根目录：`/home/kuro/rk3506/OK3506B-S12_Linux6.1.99/SDK/OK3506_Linux_Source`

## 1. 最终结论

这次问题最终根因在 **U-Boot 侧**。

在问题定位过程中，DTS、reserved-memory、`amp` 分区内容、RTOS 的 UART1 配置、Linux 侧 RPMsg 框架其实都已经基本正确。真正的卡点是：**旧版 U-Boot 没有在启动流程里自动加载并拉起 AMP 固件 `amp2`，因此 CPU2 上的 RTOS 没真正起来。**

更换成带 AMP FIT loadables 启动逻辑的 U-Boot 后，整套链路恢复正常：

- U-Boot 成功加载 `amp2`
- CPU2 成功拉起
- RTOS 成功启动
- Linux 侧 `rockchip-rpmsg` 正常 online

## 2. 根因摘要

### 旧版 U-Boot 阶段

板子之前运行的是这版 U-Boot：

- `U-Boot 2017.09-g3fe339f #zmx (Jul 10 2025 - 10:06:23 +0800)`

这一阶段的现象：

- Linux 侧 `rockchip-rpmsg` probe 成功
- `amp` 分区存在，且内容已经是新的 UART1 RTOS 镜像
- Linux 侧 `rockchip_amp` 驱动也已 probe
- 但 RTOS 没有通过 U-Boot 启动链被真正拉起

### 更换 U-Boot 之后

板子后来运行的是这版 U-Boot：

- `U-Boot 2017.09-gd76af45-dirty #zmx (Apr 25 2025 - 16:38:49 +0800)`

这版日志里出现了明确的 AMP 启动证据：

- `## Loading loadables from FIT Image ...`
- `Trying 'amp2' loadables subimage`
- `Description:  rtos-core2`
- `Loading loadables ... to 0x03e00000`
- `AMP: Brought up cpu[f02] with state 0x10, entry 0x03e00000 ...OK`
- `I/TC: Secondary CPU 2 initializing`
- `I/TC: Secondary CPU 2 switching to normal world boot`

这组日志足以证明：**之前的问题就是 U-Boot 没把 AMP 启动链打通。**

## 3. 当前已验证可用的配置

### 顶层产品 defconfig

- `device/rockchip/.chips/ok3506/OK3506-S-MINI_amp_nand_defconfig`

关键字段：

- `RK_AMP=y`
- `RK_AMP_FIT_ITS="amp_linux.its"`
- `RK_AMP_RTT_TARGET="rk3506-32"`
- `RK_UBOOT_CFG_FRAGMENTS="rk-amp"`
- `RK_KERNEL_DTS_NAME="OK3506-S-MINI_amp_nand"`
- `RK_PARAMETER="parameter-mini-amp-nand.txt"`

### AMP ITS

- `device/rockchip/.chips/ok3506/amp_linux.its`

关键点：

- 定义了 `amp2`
- 目标 CPU 是 `0xf02`
- 加载地址是 `0x03e00000`
- RTOS 配置使用：
  - `board/evb1/ok3506_amp_uart1_defconfig`

### RTOS 串口方案

- 控制台设备：`uart1`
- 引脚：`GPIO0_A0` / `GPIO0_A1`
- 复用模式：RMIO
- 波特率：`1500000`

关键文件：

- `rtos/bsp/rockchip/rk3506-32/board/evb1/ok3506_amp_uart1_defconfig`
- `rtos/bsp/rockchip/rk3506-32/rtconfig.h`
- `rtos/bsp/rockchip/rk3506-32/board/common/iomux_base.c`
- `rtos/bsp/rockchip/rk3506-32/board/common/board_base.c`
- `rtos/bsp/rockchip/rk3506-32/applications/main.c`

预期串口输出：

- `Hi, this is RT-Thread!!`
- `[RTT][UART1] heartbeat=%u`

### Linux AMP DTS

- `kernel-6.1/arch/arm/boot/dts/OK3506-S-MINI_amp_nand.dts`

关键点：

- AMP 相关 reserved-memory 节点已定义
- `rockchip_amp` 使用 UART1 pinctrl
- `amp-irqs` 已从 UART4 IRQ 70 改为 UART1 IRQ 67

### 运行时关键分区

板子运行时确认的相关分区：

- `boot` -> `mtd2`
- `amp` -> `mtd6`

## 4. 当前方案成立的关键证据

### U-Boot 侧证据

确认 U-Boot 已成功拉起 AMP 的日志：

- `Trying 'amp2' loadables subimage`
- `AMP: Brought up cpu[f02] ...OK`
- `Secondary CPU 2 initializing`

### Linux 侧证据

Linux 侧 RPMsg 正常的日志：

- `rockchip-rpmsg 3c00000.rpmsg: rockchip rpmsg platform probe.`
- `rockchip-rpmsg 3c00000.rpmsg: assigned reserved memory node rpmsg-dma@3d00000`
- `virtio_rpmsg_bus virtio0: rpmsg host is online`

### `amp` 分区内容证据

读取 `mtd6` 后确认分区内容正确：

- `uart1`
- `[RTT][UART1] heartbeat=%u`

## 5. 两个容易误判的点

### 5.1 `/sys/rk_amp/boot_cpu` 不是这次问题的主判据

`/sys/rk_amp/boot_cpu` 存在，只能说明 Linux 侧 `rockchip_amp` 驱动 probe 成功。

它不能单独证明 CPU2 是否已经真正启动，原因是：

- 当前 RK3506 DTS 路径没有定义 `amp-cpus`
- 所以执行：
  - `echo on 0xf02 > /sys/rk_amp/boot_cpu`
  - 可能会得到 `rk_amp: cpu[f02] is unavailable`
- 执行：
  - `echo status 0xf02 > /sys/rk_amp/boot_cpu`
  - 可能会得到 `failed to get cpu[f02] status, ret=5!`

这些信息不能拿来否定 U-Boot 侧已经成功启动 AMP。

### 5.2 reserved-memory overlap 警告不是致命错误

U-Boot 日志里有：

- `amp@3e00000 ... overlap with "rtos-core2"`

对当前 AMP 布局来说，这更像是预期内警告，因为：

- `rtos-core2` 就是被装载到 `0x03e00000`
- `amp@3e00000` 本来就是给 AMP 固件预留的区域

因此这条警告不是本次启动失败的主因。

## 6. 后续排查/验证常用命令

### 检查 `amp` 分区内容

```bash
dd if=/dev/mtdblock6 bs=4K count=64 2>/dev/null | strings | grep -E 'uart1|uart4|\[RTT\]|heartbeat'
```

### 检查 Linux 侧 RPMsg

```bash
dmesg | grep -i rpmsg
ls -l /sys/bus/rpmsg/devices
```

### 检查 Linux 侧 AMP 驱动

```bash
ls -l /sys/rk_amp
cat /sys/rk_amp/boot_cpu
```

### 直接看 RTOS UART 输出

使用 UART1：

- 引脚：`GPIO0_A0` / `GPIO0_A1`
- 波特率：`1500000`

预期输出：

```text
Hi, this is RT-Thread!!
[RTT][UART1] heartbeat=...
```

## 7. 推荐构建顺序

先进入 SDK 根目录：

```bash
cd /home/kuro/rk3506/OK3506B-S12_Linux6.1.99/SDK/OK3506_Linux_Source
```

选择产品配置：

```bash
./build.sh lunch
```

选择：

```text
OK3506-S-MINI_amp_nand_defconfig
```

然后建议按这个顺序编译：

```bash
./build.sh uboot
./build.sh amp
./build.sh firmware
./build.sh updateimg
```

刷机时，建议至少同步更新：

- `uboot`
- `boot`
- `amp`

或者直接刷新的 `update.img`。

## 8. 建议备份的已知可用产物

建议把当前已验证可用的这些文件单独备份：

- `u-boot/uboot.img`
- `rockdev/boot.img`
- `rockdev/amp.img`
- `rockdev/update.img`

另外建议同时记录：

- U-Boot 版本串
- Linux 内核版本串
- 当前使用的产品 defconfig
- UART1 / RMIO / 1500000 串口参数

## 9. 最终一句话总结

这次 RK3506 AMP 启动问题的根因不是 DTS、RTOS 镜像内容或 Linux 侧 RPMsg，而是 **U-Boot 没有自动加载并启动 `amp2`**。更换成支持 AMP FIT loadables 启动链的 U-Boot 后，CPU2 和 RTOS 已成功启动。
