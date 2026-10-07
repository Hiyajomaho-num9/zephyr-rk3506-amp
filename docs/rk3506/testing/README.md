# 测试应用与镜像对照

复核：2026-10-06。所有路径相对 SDK 根；构建命令见 [BUILD.md](../BUILD.md)。
默认 `./build.sh amp` 是 pingpong，切换应用会覆盖当前 `output/firmware/amp.img`，
而已有 `output/releases/` 的归档保留原版本。`updateimg` 打包当时的分区文件，不自动切回默认应用。

## 已有目标

| 目标 | 应用 / 配置 | 可追溯镜像 | 验收边界 |
|---|---|---|---|
| RPMsg / CAN 默认应用 | `samples/subsys/ipc/rpmsg/rk3506_pingpong` | `output/releases/update-20260928-025357Z-sdk-root/amp.img` 及同目录 update.img | 9/28 编译通过，当前新组合未上板；ping 有轮询兜底 |
| SPI 内部回环 | `samples/rk3506/spi_loopback` | `output/releases/update-20261006-020523Z-spi-internal/amp.img` 及同目录 update.img | 10/06 重编/回拆通过，SPI0/SPI1 不接线，待上板 |
| SPI1 引脚回环 | 同应用 + `external-spi1.overlay` | `output/validation/sdk-layout-20260928-024546Z/spi1-external-amp.img` | 待上板；SoM 78 ↔ 76；SPI1 CSN1，SPI0 内部回环 |
| GPIO API | `tests/drivers/gpio/gpio_basic_api` | `output/validation/sdk-layout-20260928-024546Z/gpio-basic-api-amp.img` | GPIO3_A6/A7（SoM 129 ↔ 128）、IRQ group 3；待上板 |
| GPIO shell | 默认 pingpong + `debug/gpio-shell.conf`、`debug/enable_gpio3.overlay` | `output/validation/sdk-layout-20260928-024546Z/gpio-shell-amp.img` | 只开放 GPIO3_A6/A7；新配置待上板 |
| GPIO 按键模拟 | `samples/rk3506/gpio_power_cycle` | `output/releases/amp-powercycle-gpio0a2-20260924.img` 是历史版 | 不在 9/28 五目标验证中；若测试现有源码应重新构建，不能沿用旧版时序结论 |
| CAN API 专用测试 | `tests/drivers/can/rk3506_api` | 本轮没有对应专用归档 | 不在 9/28 五目标验证中；不能把 pingpong 包含 CAN 当作 API 测试通过 |

历史 CAN 报告已有 STM32G4 外部 TX/RX/filter/IRQ 验收及 CAN API build-only 记录。
其范围是 Classical CAN 标准 ID、DLC 不超过 8、normal mode、500 kbit/s；
这些成果保留为旧基线，新 GIC/cache/Linux 组合仍需回归。

上表 validation 中的 AMP 文件是 9/28 编译快照，不是新增上板报告，也没有完整升级包的独立交付清单。
SPI 的 9/25 两份历史镜像也保存在 `output/releases/`，完整哈希、测试输出和接线见 [SPI.md](SPI.md)。

## 当前 Linux 基线

9/28 pingpong 包和 10/06 SPI 包的 boot.img 均已包含时钟持有和 PWM0_CH1 禁用。
当前 `output/firmware/` 是 10/06 SPI 内部回环完整包对应产物。
SPI 使用此基线时，应按 SPI.md 第 5 节验收：时钟一直开启、`gated 0`，两次自检通过，长跑无新增失败。
SPI 指南第 3/4 节的 Linux 关时钟事件是旧基线对照，不能拿它作为新 boot.img 的成功标准。

GPIO/CAN 中断另受尚未修复的 Linux GIC 初始化影响。SPI 是轮询驱动，SPI 通过不能证明 IRQ 正常；
RPMsg pingpong 也有轮询兜底，需要独立验证 mailbox 中断。

## 引脚与恢复

- GPIO2 的 NAND/RMII 引脚继续留给 Linux；GPIO3_A0–A5 是 TF 卡，仅测试 A6/A7。
- CAN0 使用 GPIO0_B6/C0，因此 SPI1 用 CSN1，SPI0 当前只做内部回环。
- SPI 的 `external-pins.overlay`、`gpio-cs-build.overlay` 是编译检查变体，不作为当前上板目标。
- SPI 与独立 GPIO 应用不创建 RPMsg 端点；这时 ping 超时不能用来判定测试失败。
- 测试结束后，在 SDK 根不带应用/overlay 环境变量执行 `./build.sh amp`，恢复默认 pingpong 分区产物。
  若要打完整包，再执行 `./build.sh updateimg`；归档目录中的原 update.img 不受此影响。

## 记录板上结果

每次记录镜像路径和 SHA256、实际更新的分区、Linux 启动日志、UART1 日志、接线与测试结论。
板上当前版本未确认；编译机的 `output/firmware/` 不能代表板上正在运行的版本。
验收后把结果写回 [STATUS.md](../STATUS.md)，保留原始日志到 `output/validation/` 的独立目录。
