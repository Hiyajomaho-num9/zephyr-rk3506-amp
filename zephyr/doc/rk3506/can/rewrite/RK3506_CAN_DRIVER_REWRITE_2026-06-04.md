# RK3506 CAN driver rewrite notes

日期：2026-06-04

## 目标

把旧的 CAN bring-up/bridge 代码替换为 Zephyr CAN API 驱动路径：

- 不再从 RPMsg sample 或 SoC probe 里裸写 CAN0 发送寄存器。
- 不再保留 `CONFIG_RK3506_SAMPLE_CAN_TX*` 这类应用层固定发包 Kconfig。
- RPMsg sample 只作为 Linux 侧测试入口，内部通过 `can_send()`、`can_add_rx_filter()`、`can_get_state()` 调用标准 CAN API。
- CAN 控制器逻辑集中在 `drivers/can/can_rk3506.c`。
- CAN driver 不再直接写 CRU clock/reset、GPIO IOMUX、RMIO pinmux。
- CAN driver 只消费 DTS 声明的 `clocks`、`resets`、`pinctrl-0` 资源。

## 已移除的旧路径

- `soc/rockchip/rk3506/rk3506_can_probe.c`
- `CONFIG_SOC_RK3506_CAN_REGISTER_PROBE`
- `CONFIG_SOC_RK3506_CAN_LOOPBACK_PROBE`
- `CONFIG_SOC_RK3506_CAN_RPMSG_BRIDGE`
- `CONFIG_SOC_RK3506_CAN0_RMIO14_16_PINMUX`
- SoC header 里的旧 CAN bridge/probe 函数声明和诊断结构。

旧 bring-up 文档仍作为历史记录保留在 `docs/can/`，但不再参与构建路径。

## 新驱动能力

`drivers/can/can_rk3506.c` 现在实现：

- `start`
- `stop`
- `set_mode`
- `set_timing`
- `send`
- `add_rx_filter`
- `remove_rx_filter`
- `get_state`
- `set_state_change_callback`
- `get_core_clock`
- `get_max_filters`
- IRQ TX completion
- IRQ RX drain
- 软件 RX filter
- TX watchdog，避免硬件无完成中断时阻塞后续发送槽

当前主验证目标是 Classical CAN standard data frame。Phase 7.9-A 后，
CAN-FD/BRS/TDC、extended ID、RTR、listen-only、one-shot、triple-sampling
都不再作为 driver 对外能力暴露；后续必须单独补板端验证和测试矩阵后再开启。

## 2026-06-04 分层修正

旧版本曾把 clock/reset/pinmux 临时写在 CAN driver 里。这个做法不符合
Zephyr BSP 分层：driver 抢了 DTS/SoC/pinctrl/clock/reset 应该管理的底层资源。

本轮已经改为：

- `drivers/pinctrl/pinctrl_rk3506.c`
  - 负责 GPIO0 IOMUX 和 RMIO selector。
  - CAN0 PB6/PC0 只通过 board DTS 的 `pinctrl-0` 选择。
- `drivers/clock_control/clock_control_rk3506_cru.c`
  - 负责 CAN0/CAN1 CRU gate 和 CAN baud clock rate。
  - 当前支持 DTS 请求的 300 MHz CAN clock。
- `drivers/reset/reset_rk3506_cru.c`
  - 负责 CAN0/CAN1 reset assert/deassert。
- `drivers/can/can_rk3506.c`
  - 只通过 Zephyr `clock_control`、`reset`、`pinctrl` API 准备资源。
  - 只实现 CAN controller register programming 和 Zephyr CAN API。
- `boards/forlinx/ok3506b_s12/ok3506b_s12_amp_uart1.dts`
  - 声明 CAN0 使用 PB6 TX / PC0 RX。
  - 声明 board 默认 `bitrate = <500000>`、`sample-point = <870>`。

已经删除的旧 Kconfig：

- `CONFIG_CAN_RK3506_FORCE_CAN0_300M`
- `CONFIG_CAN_RK3506_CAN0_RMIO14_16_PINMUX`
- `CONFIG_CAN_RK3506_SOC_CLOCK_RESET_FIXUP`

结论：

- BSP/SoC/DTS 描述资源。
- pinctrl/clock/reset provider 操作底层寄存器。
- CAN driver 消费资源并实现标准 CAN API。
- RPMsg sample 是应用层测试入口，不再绕过 CAN API。

## 本轮构建产物

- `output/firmware/amp.img`
- `zephyr-rk3506/out/zephyr.bin`
- `zephyr-rk3506/out/zephyr.elf`

SHA256：

```text
c259a6b5ae37f9c02f330ef321e6d77dccd2f00a723f1f0e69ca85452713b21a  output/firmware/amp.img
b0d6aecbb575cbc541cc50219dedb6d9ab9e6fdd82efa5ffb5e162f5855f20f2  zephyr-rk3506/out/zephyr.bin
72eb992828491a8d19f7dc97493cc407422d377ffdfe6643d136faa63389078a  zephyr-rk3506/out/zephyr.elf
```

## 2026-06-04 ret=-22 修正

现象：

```text
[RK3506][CAN-RPMSG] api tx ret=-22 id=0x01b len=8 req=0 txok=0 state=4 rxerr=0 txerr=0
```

原因：

- `state=4` 是 Zephyr `CAN_STATE_STOPPED`。
- `ret=-22` 是 `-EINVAL`，发生在 `can_start()` 内部 timing 编码阶段。
- Vendor Linux `rk3576_canfd` 约束是 `brp_inc = 2`，但 Zephyr 通用
  `can_calc_timing()` 不知道 RK3506/RK3576 的偶数 BRP 约束，某些
  bitrate/sample-point 组合可能算出硬件无法编码的 timing。

修正：

- driver 不再写死 500 kbit/s fallback。
- driver 内部实现 RK3506/RK3576 感知的 timing 搜索，搜索时只尝试偶数
  prescaler，等价于 vendor Linux `brp_inc = 2`。
- DTS 的 `bitrate` / `sample-point` 仍是标准输入；应用也仍可通过
  Zephyr `can_set_timing()` 传入自定义 timing。
- 这只修正启动 timing，不重新引入旧的 probe/loop/裸寄存器发包路径。

## 推荐测试顺序

刷入 `output/firmware/amp.img` 后，先确认 RPMsg：

```sh
ls -l /sys/bus/rpmsg/devices
/userdata/rk3506_rpmsg_char_ping
```

确认 CAN TX：

```sh
/userdata/rk3506_rpmsg_can 0x1b 0101030301000000
```

如果要测试新 `txrx` 路径，用通用 RPMsg char 工具直接发送命令：

```sh
/userdata/rk3506_rpmsg_char_ping /dev/rpmsg_ctrl0 'can:txrx:0x1b:0101030301000000:1000' 0x3003
```

预期 Zephyr 端只打印 `CAN-RPMSG api tx` 或 `CAN-RPMSG api txrx`，不应再出现：

- `CAN-PROBE`
- `CAN-LOOP`
- `[CAN-RPMSG][DIAG]`
