# GPIO0_A2 单次按键模拟

本应用通过 Zephyr GPIO API 调用 `drivers/gpio/gpio_rk3506.c`，
不使用 Linux GPIO、不在应用中读写裸寄存器。

## 时序

以应用成功执行 `gpio_pin_configure_dt(..., GPIO_OUTPUT_HIGH)` 为起点：

1. GPIO0_A2 输出高电平，保持 50 秒。
2. 输出低电平，保持 250 毫秒。
3. 输出高电平，保持 20 秒。
4. 再次输出低电平，保持 250 毫秒。
5. 恢复高电平，之后不再翻转；每次应用启动只执行一轮。

计时由内核睡眠接口完成，实际边沿可能因调度稍有延后。
上电到 Zephyr 初始化 GPIO 之前的电平由硬件和启动程序决定。

## 分层与验收

- `src/main.c`：只负责按键时序和错误报告。
- `app.overlay`：选择 gpio0 pin 2、GPIO 复用，屏蔽本应用对同 bank 其他引脚的访问。
- GPIO、clock_control、pinctrl 驱动：分别负责电平/方向、时钟和复用。
- A2 的 `drive-push-pull` 放在 pinctrl 节点中，由 pinctrl 驱动配置 IOC；
  应用和 GPIO 驱动不直接改 pinmux/IOC 电气寄存器。
- 本例不申请 GPIO 中断，不接管 Linux 的 GPIO bank IRQ。
- Linux AMP 设备树必须禁用 `pwm0_4ch_1`（`/pwm@ff931000`）；原来的
  PWM 默认 pinctrl 占用了 A2，会在 Linux 启动时覆盖 Zephyr 的 GPIO 复用。
- 这是独立 GPIO 应用，不创建 RPMsg 端点；不要以 RPMsg ping 是否响应判定成功。

上板后观察原 UART1 控制台，并用逻辑分析仪检查 A2。日志应依次显示
`HIGH 50s -> LOW 250ms -> HIGH 20s -> LOW 250ms -> HIGH forever`，
约 50000 ms 的 LOW、50250 ms 的 HIGH、70250 ms 的 LOW、70500 ms 的 HIGH，
最后显示 `one-shot complete`。GPIO API 失败时打印错误并停止时序。

## pinctrl 配置

使用 `RK3506_PINMUX(bank, pin, mux, rmio_func)`，其中 bank 为 GPIO0～4，
pin 为 bank 内编号：A0=0、B0=8、C0=16、D0=24。
旧的 `RK3506_PINCTRL(组, 组内引脚, IOC复用, RMIO功能)` 保留为 GPIO0 兼容写法。
两者都编码为单个 32 位 cell；请使用宏，不沿用旧的手写数值。
`pinctrl-0`/`pinctrl-names = "default"` 仍由 GPIO 或外设驱动正常应用。

驱动已实现 GPIO0～4 的 87 个双向 pad，支持合法复用、上下拉、输入使能、
Schmitt、驱动强度等级及 slew-rate；具体范围见 GPIO/pinctrl binding。
GPIO0_D0 和 GPIO4 不支持硬件开漏。GPIO4_B0～B3 的上下拉等电气属性共用，
修改这些属性的 pinctrl state 必须同时包含四脚、且属性一致。
GPIO4_A0～A5 是 D-PHY 输出专用脚，尚未实现 PHY GPIO 模式，明确保留不可用。
这些是驱动实现范围，不代表所有引脚已经上板验证；本示例仍然只使用 A2。
