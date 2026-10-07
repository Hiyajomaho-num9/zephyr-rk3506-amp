# ZCode 会话同步文档 — zephyr-rk3506-amp 工作状态

> 本文档由桌面机侧 ZCode 会话（sess_53d9e699，Windows 10.0.0.5）维护。
> **任何在编译机上启动的 ZCode 会话（如 sess_73de19c5）开工前先读本文件**，
> 这是两台机器之间唯一可靠的同步通道（ZCode 会话库按主机隔离存储，跨主机不可读）。

最后更新：2026-09-03

## 1. 仓库与当前状态

- GitHub: https://github.com/Hiyajomaho-num9/zephyr-rk3506-amp （main = `b3932a931cb`）
- 本地工作副本: `~/my_code/zephyr-rk3506-amp`（官方 Zephyr 以 subtree 形式在 `zephyr/` 子目录）
- 提交链（main）:
  ```
  b3932a931cb  drivers: gpio: add Rockchip RK3506 GPIO driver          ← 最新
  9be650dd290  Merge Zephyr upstream main (2f0bc11264a) into zephyr/ subtree
  31d137389e2    └─ Squashed upstream v4.4.0 → 2026-09-01 main
  e6092401aad  Apply RK3506 AMP customization on top of Zephyr v4.4.0
  ```

## 2. 上游合并要点（已完成的认知）

- 上游 `zephyrproject-rtos/zephyr` main 合入 `zephyr/` 子树，无 forlinx/rk3506 内容冲突
- AMP 定制层 3815 文件（A=3789/M=11/D=15）：can_rk3506、forlinx 板级、RK3506 SoC/时钟/pinctrl/复位、RPMsg
- M 文件：`arm_mmu.c`（`CONFIG_RK3506_PRIVATE_ICACHE` 下关 DCACHE）、`soc.yml`、各驱动单行注册
- D 文件（故意删除，勿恢复）：`drivers/i2c/target/*`（eeprom 部分）、`tests/boards/neorv32`、`scripts/tests/build/*`
- 合并冲突处理原则：**AMP 定制全保留，上游新功能全纳入**

## 3. GPIO 驱动（b3932a931cb，已推送）

- 驱动: `zephyr/drivers/gpio/gpio_rk3506.c`（358 行，TRM Part 1 §17）
- 5 个 32-pin bank（gpio0-4），寄存器 L/H 半字 + **高位 per-bit 写使能**（bit n+16 门控 bit n）
- 中断：电平/边沿/**双沿走独立 GPIO_INT_BOTHEDGE 寄存器**、EOI 应答
- 时钟：`clock_control_on` 走 CRU；**gpio0 pclk 门控在 PMU CRU (0xFF9B0000) GATE_CON00 bit8**；
  gpio1=GATE_CON03(0x80c)bit8、gpio2=GATE_CON12(0x830)bit14、gpio3/4=GATE_CON13(0x834)bit0/2
- dts: gpio0-4 默认 disabled，每 bank 4 个 GIC SPI（SPI 0-19，硬件号-32）
- binding: `dts/bindings/gpio/rockchip,rk3506-gpio.yaml`

## 4. 构建环境（重要）

- **python 必须 ≥3.12**：用 `~/venv-zephyr-py312`（持久，勿用 /tmp/zpy312——重启会丢）
- cmake 3.28.4: `~/.local/bin/cmake`（PATH 需含 `~/.local/bin`）
- 工具链: `/usr/bin/arm-none-eabi-`（gcc 10.3.1）
- 上游 main 需要装 **west**（否则 Kconfig.modules 为空 → CMSIS 双定义 Kconfig 崩溃）
- 构建命令模板：
  ```sh
  export PATH=$HOME/venv-zephyr-py312/bin:$HOME/.local/bin:$PATH
  cd ~/my_code/zephyr-rk3506-amp/zephyr
  export ZEPHYR_BASE=$(pwd)
  cmake -GNinja -B /tmp/build -S samples/subsys/ipc/rpmsg/rk3506_pingpong \
    -DBOARD=ok3506b_s12_amp_uart1 \
    -DZEPHYR_TOOLCHAIN_VARIANT=cross-compile -DCROSS_COMPILE=/usr/bin/arm-none-eabi- \
    -DPython3_EXECUTABLE=$HOME/venv-zephyr-py312/bin/python \
    -DEXTRA_ZEPHYR_MODULES="$PWD/external_modules/hal/cmsis;$PWD/external_modules/lib/picolibc"
  cmake --build /tmp/build
  ```

## 5. 已知集成坑（勿踩）

1. 新上游 `gpio.h` 不再自动包含 `gpio_utils.h`，`GPIO_PORT_PIN_MASK_FROM_DT_INST` 需
   显式 `#include <zephyr/drivers/gpio/gpio_utils.h>`
2. `GPIO_RK3506` 必须 `select CLOCK_CONTROL`
3. DTS overlay 引用 gpio 前需 `&gpioX { status = "okay"; }` 且 overlay 头部
   `#include <zephyr/dt-bindings/gpio/gpio.h>`
4. 宏内嵌 `COND_CODE_1` 包含逗号会炸，构造静态结构体用独立定义 + `COND_CODE_1` 只选指针

## 6. 未完成事项（下一步）

- [ ] GPIO 硬件实测（需接跳线）：gpio2 pin4↔pin5 短接跑 `tests/drivers/gpio/gpio_basic_api`
- [ ] 待实测项：双沿触发、EOI 清边沿中断、gpio0 PMU 域门控
- [ ] 后续外设适配候选：PWM / I2C / SPI（TRM §31 / §32 / §29）

## 7. 其他资源位置

- TRM 全文缓存: `/tmp/rk3506_trm.txt`（7 万行，重启会丢；原件
  `~/rk3506/OK3506B-S12_Linux6.1.99/原厂资料/Rockchip RK3506 TRM Part 1 V1.0-20240902.pdf`）
- AMP 布局基线: `~/rk3506/phase-archive/phase5.1-zephyr-amp-layout-20260515/rk3506_amp_layout.h`
  （0x03b00000 shmem / 0x03c00000 rpmsg vring / 0x03d00000 rpmsg-dma）
- 相关会话参考: 桌面机会话 sess_53d9e699（本文件即其交接产物）
