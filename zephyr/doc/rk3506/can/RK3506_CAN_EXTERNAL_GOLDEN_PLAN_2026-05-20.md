# RK3506 CAN0 外部联调黄金基线计划

> 2026-05 的历史计划与排障记录，后续外部验收见同目录 CAN bring-up notes。
> 当前镜像和待办见 [STATUS.md](../../../../docs/rk3506/STATUS.md)；旧路径和测试命令需按当前构建指南核对。

日期：2026-05-20

目标：停止围绕 GPIO 和内部 loopback 反复兜圈，先建立一个真实外部 CAN 的黄金基线，再把 Zephyr 的寄存器序列对齐过去。

## 当前事实

1. RPMsg 已验证：
   - Linux `/userdata/rk3506_rpmsg_char_ping` 可以收到 Zephyr `pong:1`。
   - RPMsg 通道名：`rpmsg-ap3-ch0`。
   - Zephyr 端 endpoint：`0x3003`。

2. CAN 内部 loopback 已验证：
   - `CAN-LOOP PASS` 已出现。
   - 这只能证明 CAN IP 内部收发路径可用，不能证明外部 PAD / bit timing / Linux 参考序列正确。

3. OK3506B-S12 当前外部 CAN0 选择：
   - `CAN0_TX = GPIO0_PB6 / RM_IO14`
   - `CAN0_RX = GPIO0_PC0 / RM_IO16`
   - GPIO 翻转/输入已验证，不再重复做 GPIO 测试，除非后续 Linux 黄金基线也失败且有新的硬件证据。

4. 当前实际启动链路使用 `OK3506-S-MINI_amp_nand.dts`：
   - 它包含 `OK3506-S-MINI_linux_nand.dts`，再包含 `OK3506-S-MINI-common.dtsi`。
   - 不是 `OK3506-S-common.dtsi`。之前如果只改 `OK3506-S-common.dtsi`，DTB hash 不变，说明改错目标文件。
   - `OK3506-S-MINI-common.dtsi` 中 `i2c0` 使用 `RM_IO11/RM_IO12`，不占用 PB6/RM_IO14 或 PC0/RM_IO16。
   - 因此本轮只在 active MINI DTS 里覆盖 CAN0 到 PB6/PC0，不禁用 i2c0/can1。

5. Zephyr 当前外部 TX 失败现象：
   - RPMsg 命令能到达 Zephyr。
   - CAN 寄存器能被写入。
   - 但外部 TX 没有形成稳定可观测 CAN 帧，返回中常见：
     - `ret=-5`
     - `txok=0`
     - `err=0x000c0000`
     - `txerr` 增长到 256
   - 这说明应优先怀疑 CAN 初始化/发送序列、bit timing、外部 ACK/收发器环境，而不是 RPMsg 或 GPIO。

## 黄金基线方法

### Phase A：Linux SocketCAN 真实外部基线

目的：先证明同一块板、同一组 PB6/PC0、同一 CAN 收发器/STM32G4/PCAN 环境，在 Linux 官方驱动下能不能发送。

本轮修改：

1. 临时 patch Linux active DTS：
   - CAN0 pinctrl 改为：
     - `rm_io14_can0_tx`
     - `rm_io16_can0_rx`
   - 目标文件：`kernel-6.1/arch/arm/boot/dts/OK3506-S-MINI-common.dtsi`
   - 不修改 `OK3506-S-common.dtsi`，避免污染非 MINI 板型。

2. 新增 Linux 用户态工具：
   - `/userdata/rk3506_socketcan_send`
   - 通过 AF_CAN/CAN_RAW 直接发送标准 Classical CAN 帧。

3. 新增寄存器 dump 脚本：
   - `/userdata/rk3506_can_dump_regs.sh`
   - dump CRU、pinmux、CAN0 关键寄存器，作为 Linux 黄金参考。

板端测试命令：

```sh
ip link set can0 down
ip link set can0 type can bitrate 500000 restart-ms 100
ip link set can0 up

/userdata/rk3506_socketcan_send can0 0x11b 0101030301000000 100 1000
ip -details -statistics link show can0
/userdata/rk3506_can_dump_regs.sh linux-after-tx
```

验收：

- 如果 Linux SocketCAN 在 PB6/PC0 上也发不出外部 CAN 帧：
  - 停止 Zephyr CAN 调试。
  - 优先查外部收发器、终端电阻、TX/RX 方向、STM32G4/PCAN bit timing、板级连接。

- 如果 Linux SocketCAN 可以发出并被 STM32G4/PCAN 收到：
  - Linux dump 出来的寄存器就是 Zephyr 对齐目标。
  - 再进入 Phase B。

### Phase B：Zephyr 分步寄存器对齐

目的：不再用一个 `can:tx` 大命令黑盒测试，而是把 CAN 控制过程拆成可比对步骤。

计划新增 RPMsg 子命令：

```text
can:ctl:init
can:ctl:load:<id>:<hex-data>
can:ctl:req
can:ctl:dump
can:ctl:reset
```

每一步都返回 CAN0 寄存器快照。

对齐顺序：

1. `can:ctl:init`
   - 对比 Linux `ip link set can0 up` 后的寄存器。

2. `can:ctl:load`
   - 只写 TXID/TXFIC/TXDAT，不触发发送。
   - 对比 Linux 发送前后 TX 寄存器格式。

3. `can:ctl:req`
   - 只触发一次 TX request。
   - 观察 `CMD/INT/STATE/ERR/TXERRORCNT`。

4. `can:ctl:reset`
   - 总线错误或 bus-off 后强制回到干净状态，避免多轮测试污染。

### Phase C：Zephyr 外部 CAN 正式验收

在 Linux 黄金基线通过、Zephyr 寄存器对齐后，才跑：

```sh
/userdata/rk3506_rpmsg_can_tx 0x11b 0101030301000000 100 1000
```

验收：

- Zephyr 返回：
  - `ret=0`
  - `req=100`
  - `txok=100`
  - `txerr` 不持续增长
- STM32G4 或 PCAN 能看到 ID `0x11b`、8 字节 `01 01 03 03 01 00 00 00`。

## 当前 ranked hypotheses

1. **H1：Linux DTS pinctrl 与 OK3506B-S12 实际接线不一致。**
   - 预测：改 Linux CAN0 到 PB6/PC0 并禁用冲突节点后，SocketCAN 可以作为黄金基线。

2. **H2：Zephyr CAN init/xmit 序列与 Linux `rk3576_canfd.c` 仍有差异。**
   - 预测：Linux golden dump 中 `STR_CTL/NBTP/INT_MASK/AUTO_RETX/MODE` 等寄存器与 Zephyr 不一致；对齐后 TX 行为改变。

3. **H3：外部总线缺 ACK 或收发器环境不完整。**
   - 预测：Linux SocketCAN 也会出现 ACK error / txerr 增长 / 无 PCAN 接收。

4. **H4：bit timing clock 假设错误。**
   - 预测：Linux golden 的 `NBTP` 与 Zephyr 当前 `0x000e040e` 不一致；对齐 Linux 后波形速率正确。

5. **H5：Zephyr 错误恢复不干净，导致后续测试被 bus-off/error-passive 状态污染。**
   - 预测：加入 `can:ctl:reset` 或每次 TX 失败后重启 CAN 控制器，后续测试结果更稳定。

## 本轮执行边界

本轮只做 Phase A：

- 新增 Linux SocketCAN 工具。
- 新增 Linux CAN dump 脚本。
- 临时 patch Linux DTS 用 PB6/PC0 做 CAN0。
- 生成 `update.img` 给板端做黄金基线。

不做：

- 不重复 GPIO PB6/PC0 测试。
- 不继续盲改 Zephyr CAN 寄存器。
- 不把临时 DTS 改动当 upstream-ready 方案。

## Phase A 执行结果

执行时间：2026-05-20

已完成：

1. active Linux DTS 已确认并修改：
   - 文件：`kernel-6.1/arch/arm/boot/dts/OK3506-S-MINI-common.dtsi`
   - `can0` 已设置为：
     - `pinctrl-0 = <&rm_io14_can0_tx &rm_io16_can0_rx>;`
     - `status = "okay";`
   - 编译后的 `OK3506-S-MINI_amp_nand.dtb` 反编译确认：
     - `can@ff320000 status = "okay"`
     - `pinctrl-0 = <0x5f 0x60>`
     - `0x5f = rm-io14-can0-tx`
     - `0x60 = rm-io16-can0-rx`
     - `can@ff330000 status = "disabled"`

2. 已新增并内置 userdata 工具：
   - `/userdata/rk3506_socketcan_send`
   - `/userdata/rk3506_can_dump_regs.sh`

3. 构建命令：

```sh
./build.sh kernel
./build.sh extra-parts
./build.sh updateimg
```

4. 构建产物：

```text
output/firmware/update.img -> output/update/Image/update.img
update.img size = 71846474 bytes
```

SHA256：

```text
0f530ccca0421b26a0d803178a4170d10098bc69d77f493d2bbdcc8e65f028a6  output/firmware/update.img
f2aab628c2b7a462da4cd3664253447531d2d916fbbf88cc2a1c7105f55fc56e  output/firmware/boot.img
3639f700956fc6bfe33e3da7b367939a754a878afe794323bf3442630d07fe12  output/firmware/userdata.img
b5632cbd5059921821c9ba8d9951133d5b949ef400b99260c17737bb301b6f95  output/firmware/amp.img
```

下一步只跑 Linux SocketCAN 黄金基线，不跑 Zephyr CAN 发送。

## Phase A.1 板端 probe 结果与修正

执行时间：2026-05-22

板端现象：

```text
ip link show
  只有 lo / eth0，没有 can0

dmesg:
  rockchip-pinctrl pinctrl: pin gpio0-14 already requested by ff130000.spi; cannot claim for ff320000.can
  rockchip-pinctrl pinctrl: could not request pin 14 (gpio0-14) from group rm-io14-can0-tx
  rk3576_canfd ff320000.can: Error applying setting, reverse things back

/proc/device-tree/can@ff320000/status:
  okay

/proc/device-tree/can@ff320000/compatible:
  rockchip,rk3506-canfd rockchip,rk3576-canfd

/proc/config.gz:
  CONFIG_CAN=y
  CONFIG_CAN_DEV=y
  CONFIG_CANFD_RK3576=y
```

结论：

- CAN 内核框架和 `rk3576_canfd` 驱动存在。
- active DTB 中 `can@ff320000` 已经启用。
- `can0` 没创建的直接原因不是驱动缺失，而是 pinctrl 申请失败。
- 冲突点是 `RM_IO14 / GPIO0_PB6`：它被 `ff130000.spi` 先占用，导致 CAN0_TX 无法 claim。

修正：

- 文件：`kernel-6.1/arch/arm/boot/dts/OK3506-S-MINI-common.dtsi`
- 将 `&spi1` 和其 `spi@0` 子节点临时改为 `status = "disabled";`
- 保留 `&can0`：
  - `pinctrl-0 = <&rm_io14_can0_tx &rm_io16_can0_rx>;`
  - `status = "okay";`

边界：

- 这是 Linux SocketCAN 黄金基线用的临时板级资源选择。
- 不代表 upstream-ready 最终 DTS；最终需要根据 OK3506B-S12 原理图和功能取舍决定 SPI1 与 CAN0 的互斥配置方式。

### Phase A.2 第二轮板端 probe 结果

执行时间：2026-05-22

刷入禁用 `SPI1` 的包后，板端结果：

```text
ip link show
  仍然只有 lo / eth0，没有 can0

dmesg:
  rockchip-pinctrl pinctrl: pin gpio0-16 already requested by ff120000.spi; cannot claim for ff320000.can
  rockchip-pinctrl pinctrl: could not request pin 16 (gpio0-16) from group rm-io16-can0-rx
  rk3576_canfd ff320000.can: Error applying setting, reverse things back
```

结论：

- 第一轮修正有效：`gpio0-14 / RM_IO14 / PB6` 不再被 `ff130000.spi` 抢占。
- 第二个冲突暴露出来：`gpio0-16 / RM_IO16 / PC0` 被 `ff120000.spi` 抢占。
- 也就是说 OK3506B-S12 当前 CAN0 外部基线选择 `PB6/PC0` 同时与 active MINI DTS 中的 `SPI1/SPI0` 互斥：
  - `CAN0_TX = RM_IO14/PB6` 冲突 `SPI1`
  - `CAN0_RX = RM_IO16/PC0` 冲突 `SPI0`

修正：

- 文件：`kernel-6.1/arch/arm/boot/dts/OK3506-S-MINI-common.dtsi`
- 继续将 `&spi0` 和其 `spi@0` 子节点临时改为 `status = "disabled";`
- 目标是让 Linux `rk3576_canfd` 能完整 claim：
  - `rm_io14_can0_tx`
  - `rm_io16_can0_rx`

边界：

- 本修正仍然只服务 Linux SocketCAN 黄金基线。
- 它明确牺牲当前 DTS 中的 `SPI0/SPI1` spidev，用于换取 CAN0 外部总线验证。
