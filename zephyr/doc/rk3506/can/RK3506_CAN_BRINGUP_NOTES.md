# RK3506 CAN Bring-up Notes

> 历史基线及验收记录。以下阶段状态对应当时的镜像；当前 SDK 根目录、新版镜像和复验待办见
> [STATUS.md](../../../../docs/rk3506/STATUS.md)，构建入口见 [BUILD.md](../../../../docs/rk3506/BUILD.md)。

状态：Phase 7.7 已通过真实外部链路验证：Linux 用户态经 RPMsg 调 Zephyr CPU2，
Zephyr CAN driver 通过标准 CAN API 控制 CAN0，与 STM32G4 完成 Classical CAN
TX/RX/filter/IRQ 路径。当前进入 Phase 7.8：收敛为 upstream-ready BSP 形态，
冻结已验证范围，补文档和 build-only 回归，不再叠加临时 bring-up 旁路。

## 1. 依据来源

芯片事实以原厂 TRM 为准，Linux SDK 作为已运行实现参考，Zephyr 官方树作为代码组织/API 参考。

| 类型 | 路径/章节 | 用途 |
| --- | --- | --- |
| TRM | `原厂资料/Rockchip RK3506 TRM Part 1 V1.0-20240902.pdf`，Chapter 34 `Controller Area Network (CAN)` | 控制器能力、寄存器、bit timing、RX storage、错误/中断能力 |
| TRM | Chapter 1 地址/中断/DMAC/Matrix IO 表 | CAN0/CAN1 基址、中断号、DMA request、CAN pinmux 编号 |
| Linux DTS | `kernel-6.1/arch/arm/boot/dts/rk3506.dtsi` | RK3506 已采用的 CAN 节点、clock/reset/interrupt 命名 |
| Forlinx 板级表 | `开发板手册/2-FET3506B-S引脚复用对照表-20251028.xlsx` | OK3506B-S12/FET3506B-S 外部接口引脚冲突与最终 CAN0 RMIO 选择 |
| Linux binding | `kernel-6.1/Documentation/devicetree/bindings/net/can/rockchip_canfd.txt` | Rockchip CANFD DTS 属性命名参考 |
| Linux driver | `kernel-6.1/drivers/net/can/rockchip/rk3576_canfd.c` | 与 RK3506 DTS fallback 匹配的寄存器/初始化参考 |
| Zephyr CAN | `/home/kuro/zephyr-work/zephyr/drivers/can` | Zephyr CAN 驱动模型与 Kconfig/CMake 组织 |
| Zephyr CAN tests | `/home/kuro/zephyr-work/zephyr/tests/drivers/can` | CAN API/host/shell/timing 测试目录结构参考 |
| Zephyr CAN bus services | `/home/kuro/zephyr-work/zephyr/subsys/canbus` | ISO-TP/CANopen 等上层服务参考，首轮驱动不依赖 |

## 2. 已确认硬件事实

### 2.1 控制器类型

RK3506 使用 Rockchip 自研 RKCAN/CANFD 控制器，不是 Bosch M_CAN、NXP FlexCAN、STM32 bxCAN/FDCAN。因此不能直接套用 Zephyr 现有 `can_mcan.c` 或 `can_mcux_flexcan.c`。

TRM Chapter 34 明确控制器支持：

- Classical CAN 和 CAN-FD 标准/扩展帧收发。
- data/remote/overload/error/interframe。
- error counter、acceptance filter、bit/stuff/form/ACK/CRC error。
- maskable interrupt。
- status/error code query。
- self-test、loopback、silent、auto retransmission、auto bus-on。
- single sample / three sample。
- 独立 nominal/data bit timing。
- 2 个 TX buffer。
- Internal Storage Mode。
- DMA 能力。

### 2.2 基址与中断

| 实例 | TRM 基址 | Linux DTS `reg` | TRM global IRQ | Linux GIC_SPI | 触发 |
| --- | --- | --- | --- | --- | --- |
| CAN0 | `0xff320000`，64KB 区域 | `<0xff320000 0x1000>` | 77 | 45 | level-high |
| CAN1 | `0xff330000`，64KB 区域 | `<0xff330000 0x1000>` | 78 | 46 | level-high |

说明：

- ARM GIC SPI 编号通常是 global IRQ - 32，因此 TRM 77/78 对应 Linux `GIC_SPI 45/46`。
- TRM 地址表写 64KB，Linux DTS 只映射 `0x1000`。首轮 Zephyr 驱动按 Linux 已验证的 `0x1000` 实现；若后续使用超出 `0x0fff` 的寄存器，再扩大映射。

### 2.3 clock/reset

Linux RK3506 DTS 当前节点：

```dts
can0: can@ff320000 {
	compatible = "rockchip,rk3506-canfd", "rockchip,rk3576-canfd";
	reg = <0xff320000 0x1000>;
	interrupts = <GIC_SPI 45 IRQ_TYPE_LEVEL_HIGH>;
	clocks = <&cru CLK_CAN0>, <&cru HCLK_CAN0>;
	clock-names = "baudclk", "apb_pclk";
	resets = <&cru SRST_CAN0>, <&cru SRST_H_CAN0>;
	reset-names = "can", "can-apb";
	assigned-clocks = <&cru CLK_CAN0>;
	assigned-clock-rates = <300000000>;
	status = "disabled";
};
```

CAN1 同理，使用 `CLK_CAN1/HCLK_CAN1/SRST_CAN1/SRST_H_CAN1`，中断为 `GIC_SPI 46`。

实现约束：

- Zephyr binding 应保留 `baudclk` 与 `apb_pclk` 的命名，避免和 Linux/Rockchip 文档割裂。
- 首轮驱动必须知道 CAN functional clock 频率。Linux 参考值是 300 MHz。
- 若 Zephyr CPU2 无法安全控制 CRU reset/clock，则先通过 U-Boot/Linux/固件预配置；但必须在文档和 DTS 中显式记录这个限制。

### 2.4 pinmux

TRM Matrix IO 表里出现：

| 信号 | Matrix IO 编号 |
| --- | --- |
| `CAN1_TX` | 76 |
| `CAN1_RX` | 77 |
| `CAN0_TX` | 78 |
| `CAN0_RX` | 79 |

板级确认：

- Linux `OK3506-S-common.dtsi` 曾把 CAN0 写成 `rm_io19_can0_tx`/`rm_io20_can0_rx`，即 `GPIO0_PC3`/`GPIO0_PC4`。
- 但 `2-FET3506B-S引脚复用对照表-20251028.xlsx` 显示：
  - `GPIO0_PC3` 对应开发板功能 `SPI0_CSN0`，同时可作 `RM_IO19`。
  - `GPIO0_PC4` 对应开发板功能 `RMII_RSTn`，同时可作 `RM_IO20`。
  - `GPIO0_PB6` 对应 `SPI1_CSN0` / `RM_IO14`。
  - `GPIO0_PC0` 对应 `SPI0_CLK` / `RM_IO16`。
- RK3506 RMIO 允许在 `RM_IO14`/`RM_IO16` 上选择 `CAN0_TX`/`CAN0_RX`。
- 因此当前 OK3506B-S12 外部 CAN0 联调桥改为：

```text
CAN0_TX = rm_io14_can0_tx = GPIO0_PB6 mux 7 + RMIO func 0x1c
CAN0_RX = rm_io16_can0_rx = GPIO0_PC0 mux 7 + RMIO func 0x1d
```

这个选择避免占用 `GPIO0_PC3/GPIO0_PC4`，避免和 SPI/RMII 复位或时钟相关板级功能冲突。

注意：Linux `rk3506-pinctrl-rmio.dtsi` 里的 CAN0 RMIO mux 编码是 `43/44`，
但这不是最终写入 `RM_IO` 的 function 值。Linux `rockchip_set_rmio()` 会对
4-bit GPIO IOMUX 执行 `function = mux - 15`，所以：

```text
Linux DTS 43 -> RMIO function 28 = 0x1c = CAN0_TX
Linux DTS 44 -> RMIO function 29 = 0x1d = CAN0_RX
```

Zephyr 当前是裸写 `RM_GPIO0B6_SEL/RM_GPIO0C0_SEL`，必须写最终 function
`0x1c/0x1d`，不能直接写 DTS 中的 `43/44`。

仍未完成事项：

- 还需要确认外接 CAN 收发器方向、供电、STB/EN GPIO、终端电阻和 STM32G4 bit timing。
- 未正式实现 Zephyr CAN driver 前，不应把 CAN 节点默认设为 `okay`。

### 2.5 bit timing 初始边界

TRM Chapter 34 描述：

- `PHASE_SEG1`：1 到 16。
- `PHASE_SEG2`：1 到 8。
- `BRP`：1 到 64。
- SJW 可配。
- CAN-FD data phase 有独立时序与 TDC。

注意：Linux `rk3576_canfd.c` 里的 `can_bittiming_const` 范围比 TRM Chapter 34 文本更宽，例如 nominal `tseg1/tseg2/sjw` 到 128、`brp` 到 256 且 `brp_inc = 2`。这可能是 RK3576 兼容 IP 或寄存器字段解释差异。RK3506 Zephyr 首轮必须以 RK3506 TRM 为硬边界，Linux 范围只能作为参考，不可直接照搬。

### 2.6 RX storage / DMA

TRM 描述两类 RX storage：

- Internal Storage Mode：`STR_CTL.stm = 0`，内部 SRAM 256x32bit。
- External Storage Mode：`STR_CTL.stm = 1`，使用 `TXTHR/START_ADDR/EXTM_SIZE` 等配置；TRM 文本标注该模式目前用于调试。

Linux `rk3576_canfd.c` 的启动逻辑默认配置 internal SRAM mode：

- `CANFD_STR_CTL`
- `CANFD_STR_WTM`
- classic CAN 时使用 CAN fixed internal storage 格式。
- CAN-FD 时使用 CANFD fixed internal storage 格式。

Phase 7 首轮选择：

- 只实现 internal storage。
- 不启用 DMA。
- 不使用 external storage mode。
- 先实现 Classical CAN，CAN-FD 放到第二轮。

## 3. RK3506 CAN 寄存器初始集合

TRM Chapter 34 和 Linux `rk3576_canfd.c` 对关键寄存器偏移基本一致。

| 寄存器 | 偏移 | 首轮用途 |
| --- | --- | --- |
| `MODE` | `0x0000` | reset/normal/loopback/silent/auto bus-on 等模式控制 |
| `CMD` | `0x0004` | TX request |
| `STATE` | `0x0008` | 状态检查 |
| `INT` | `0x000c` | 中断状态 |
| `INT_MASK` | `0x0010` | 中断屏蔽/使能 |
| `NBTP` | `0x0100` | Classical CAN nominal bit timing |
| `DBTP` | `0x0104` | CAN-FD data bit timing，首轮暂不启用 |
| `TDCR` | `0x0108` | CAN-FD TDC，首轮暂不启用 |
| `BRS_CFG` | `0x010c` | CAN-FD BRS，首轮暂不启用 |
| `DMA_CTRL` | `0x011c` | DMA，首轮禁用 |
| `TXFIC` | `0x0200` | TX frame info |
| `TXID` | `0x0204` | TX frame ID |
| `TXDAT0..15` | `0x0208..0x0244` | TX data |
| `RXFIC` | `0x0300` | RX frame info |
| `RXID` | `0x0304` | RX frame ID |
| `RXTS` | `0x0308` | RX timestamp |
| `RXDAT0..15` | `0x030c..0x0348` | RX data |
| `RXFRD/RX_STR_RDATA` | `0x0400` | internal storage read path |
| `STR_CTL` | `0x0600` | internal storage mode |
| `ATF_CTL` | `0x072c` | acceptance filter enable/mode |
| `AUTO_RETX_CFG` | `0x0808` | auto retransmission |
| `RXINT_CTRL` | `0x0818` | RX interrupt control |
| `RXINT_TIMEOUT` | `0x081c` | RX interrupt timeout |
| `ERROR_CODE` | `0x0900` | error cause |
| `RXERRORCNT` | `0x0910` | RX error counter |
| `TXERRORCNT` | `0x0914` | TX error counter |
| `RTL_VERSION` | `0x0f0c` | register probe/version check |

首个寄存器探针建议读取：

- `RTL_VERSION`
- `MODE`
- `STATE`
- `INT`
- `INT_MASK`
- `RXERRORCNT`
- `TXERRORCNT`

## 4. Zephyr upstream 形态建议

### 4.1 DTS/binding

建议新增 binding：

```text
dts/bindings/can/rockchip,rk3506-canfd.yaml
```

建议 RK3506 SoC DTS 节点：

```dts
can0: can@ff320000 {
	compatible = "rockchip,rk3506-canfd";
	reg = <0xff320000 0x1000>;
	interrupts = <45 IRQ_TYPE_LEVEL_HIGH>;
	clocks = <&cru CLK_CAN0>, <&cru HCLK_CAN0>;
	clock-names = "baudclk", "apb_pclk";
	resets = <&cru SRST_CAN0>, <&cru SRST_H_CAN0>;
	reset-names = "can", "can-apb";
	status = "disabled";
};
```

注意：

- 当前 Zephyr RK3506 树的 CRU/reset binding 还不完整。若没有 CRU driver/binding，就不能硬塞无效 phandle。
- 可分两步做：先以固定 `clock-frequency = <300000000>;` 做寄存器探针/loopback；再正规化到 CRU clock/reset。
- `interrupts` 格式要跟当前 RK3506 GIC DTS cell 定义一致，不能照抄 Linux 三 cell。

### 4.2 driver

建议驱动名：

```text
drivers/can/can_rockchip_rkcanfd.c
drivers/can/Kconfig.rockchip
```

若继续作为 out-of-tree module 暂存，则放在：

```text
zephyr-rk3506/drivers/can/
```

但 upstream 准备时应按 Zephyr 官方驱动目录迁移到主树 `drivers/can/`。

实现阶段拆分：

1. register probe：只读寄存器，不启动总线。
2. loopback Classical CAN：不依赖外部收发器。
3. external Classical CAN：接 STM32G4 或 CAN 分析仪。
4. interrupt RX：从 polling 切到 IRQ，验证 ISR 路径。
5. acceptance filter：实现 `can_add_rx_filter()` 的最小可用子集。
6. CAN-FD：扩展 DLC/BRS/data bit timing/TDC。

### 4.3 samples/tests

优先复用 Zephyr 官方 CAN test/sample 语义，不新增奇怪的本地测试入口。

建议：

- RK3506 CAN API 回归按 Zephyr 官方 `tests/drivers/can/api/`
  结构放置，作为板级 driver API build-only 回归入口。
- 后续若要提交 upstream，优先把可通用的断言并入官方
  `tests/drivers/can/api` 语义；OK3506B-S12 专属回归继续用
  `platform_allow` 限定，不伪装成通用样例。
- 需要 Linux 上位机/RPMsg 参与的链路测试，记录为板级验证脚本，不作为 Zephyr upstream 通用 test。

## 5. Phase 7 计划

### Phase 7.2：DTS/binding + register probe

目标：

- 添加 RK3506 CAN binding 草案。
- 添加 CAN0/CAN1 disabled 节点。
- 写最小 probe 代码读取 `RTL_VERSION/MODE/STATE/INT/ERRORCNT`。
- 不发 CAN 帧，不改 pinmux，不影响当前 RPMsg baseline。

验收：

- Zephyr build pass。
- 运行时 UART/shell 能打印 CAN0/CAN1 寄存器读数。
- RPMsg `/userdata/rk3506_rpmsg_char_ping` 仍返回 `pong:1`。

### Phase 7.3：internal loopback Classical CAN

目标：

- 配置 internal storage mode。
- 设置 Classical CAN nominal bit timing。
- 开 loopback/self-test。
- TX 一帧 11-bit ID / DLC 8，RX 回同一帧。

验收：

- Zephyr 日志显示 TX/RX ID/DLC/data 完全一致。
- 不依赖外部 CAN 收发器。

### Phase 7.4：外部 CAN 到 STM32G4

目标：

- 确认 OK3506B-S12 CAN pin/transceiver。
- RK3506 Zephyr 发送 Classical CAN 帧到 STM32G4。
- STM32G4 回 echo。

验收：

- STM32G4 收到准确 ID/DLC/data。
- Zephyr 收到 STM32G4 echo。
- 错误计数保持可控，不进入 bus-off。

### Phase 7.5：Linux -> RPMsg -> Zephyr -> CAN

目标：

- Linux 通过 RPMsg 下发 CAN TX 请求。
- Zephyr 转发到 CAN。
- STM32G4 echo 后 Zephyr 通过 RPMsg 回 Linux。

验收：

- Linux 用户态工具完成端到端 ping。
- 长跑 1h 无 timeout/mismatch。

### Phase 7.6：CAN-FD

目标：

- 在 Classical CAN 稳定后再启用 CAN-FD。
- 增加 data bit timing、DLC > 8、BRS/TDC。

验收：

- CAN-FD 分析仪或 STM32G4 FDCAN 验证 64-byte payload。

## 6. 当前 open questions

| 问题 | 状态 | 影响 |
| --- | --- | --- |
| OK3506B-S12 哪一路 CAN 实际引出？ | resolved for Phase 7 bridge | 当前按 FET3506B-S 表使用 CAN0：GPIO0_PB6/RM_IO14 TX + GPIO0_PC0/RM_IO16 RX |
| 板上是否有 CAN transceiver？是否需要 STB/EN GPIO？ | open | 决定是否需要 `can-transceiver-gpio` |
| Zephyr CPU2 是否能安全控制 CRU CAN clock/reset？ | open | 决定 binding 中 clock/reset 能否首轮启用 |
| CAN pinmux 是否被 Linux 占用或默认配置？ | partially resolved | Phase 7 bridge 在 Zephyr CPU2 内直接写 GPIO0_IOC/RM_IO；正式 BSP 需要迁移为 pinctrl/DTS |
| RK3506 TRM bit timing 范围与 Linux rk3576 driver 范围不一致 | open | 首轮按 TRM，后续实测确认 |
| acceptance filter 到 Zephyr filter API 的映射 | open | 首轮可 accept-all，后续再优化 |
| CAN-FD data phase/TDC 参数 | open | Phase 7.6 处理 |

## 7. 不做的事

Phase 7.1/7.2 不做：

- 不把 CAN 节点默认设为 `okay`。
- 不改 RPMsg 已验证路径。
- 不启用 DMA。
- 不启用 external storage mode。
- 不直接进入 CAN-FD。
- 不假设板级引脚已经可用。

## 8. 当前实现状态

Phase 7.2 已完成源码侧实现和构建出包：

| 项目 | 结果 |
| --- | --- |
| binding | `zephyr-rk3506/dts/bindings/can/rockchip,rk3506-canfd.yaml` |
| SoC DTS | `rk3506.dtsi` 添加 CAN0/CAN1 disabled 节点 |
| probe | `soc/rockchip/rk3506/rk3506_can_probe.c` |
| Kconfig | `CONFIG_SOC_RK3506_CAN_REGISTER_PROBE`，默认关闭，sample 显式打开 |
| MMU | probe 开启时映射 CAN0/CAN1 `0x1000` device region |
| 构建 | `PATH=/home/kuro/venv-zephyr-4.4/bin:$PATH ./build.sh amp` 通过 |
| 出包 | `PATH=/home/kuro/venv-zephyr-4.4/bin:$PATH ./build.sh updateimg` 通过 |

Phase 7.2 仍坚持以下边界：

- 不把 CAN 节点默认设为 `okay`。
- 不改 CAN clock/reset/pinmux。
- 不写 CAN 寄存器，不发 CAN 帧。
- 不改 RPMsg/cache 已验证 baseline。

板端验证步骤：

1. 烧录 `output/firmware/update.img`。
2. 冷重启。
3. UART 上确认出现：

```text
[RK3506][CAN-PROBE] phase=7.2 read-only; no clock/reset/pinmux/TX changes
[RK3506][CAN-PROBE] cru gate13=...
[RK3506][CAN-PROBE] can0 base=0xff320000 ...
[RK3506][CAN-PROBE] can1 base=0xff330000 ...
```

4. Linux 侧确认 RPMsg baseline 未被破坏：

```sh
ls -l /sys/bus/rpmsg/devices
/userdata/rk3506_rpmsg_char_ping
```

预期仍然返回：

```text
rx len=6 data="pong:1"
```

Phase 7.3 首包板端结果：CAN0/CAN1 寄存器可访问、clock gate 未关闭、`RTL_VERSION=0xcfdba0e5` 有效，但 loopback 超时：

```text
[RK3506][CAN-LOOP] fail: timeout ret=-116 int=0x00000000 state=0x00000000 str=0x00000005 err=0x00000000 rxerr=0 txerr=0
```

该结果说明 Phase 7.2 的 MMIO/clock 基础是活的，但 Phase 7.3 首包的 TX/RX 完成判据或初始化序列还不对。`str=0x5` 表示 internal/external storage 都为空，当前没有 RX frame 入队。

Phase 7.3a 板端结果：

```text
[RK3506][CAN-LOOP] armed mode=0x00000035 mask=0x00000001 nbtp=0x000e040e str_ctl=0x00000104 str_wtm=0x0000006c retx=0x00000963 txfic=0x00000008 txid=0x00000123
[RK3506][CAN-LOOP] FAIL rxf=0x08080404 id=0x00000123 data=67452301:efcdab89 cmd=0x00000000 int=0x00000002 str=0x00020404 frame=1 left=4 err=0x00000000 rxerr=0 txerr=0
```

该结果说明：

- TX request 已完成：`cmd=0x00000000`。
- TX finish 已出现：`int=0x00000002`。
- RX internal storage 已入队：`str=0x00020404`，`frame=1`，`left=4`。
- 回环数据正确：`id=0x123`，payload 为 `67452301:efcdab89`。
- 无 CAN 错误：`err/rxerr/txerr` 均为 0。

7.3a 的 `FAIL` 不是硬件回环失败，而是诊断代码解析 `RXFRD` storage entry 的 DLC 字段错误：TRM 的直接 `RXFRAMEINFO` 寄存器 DLC 位在 `[3:0]`，但 Linux `rk3576_canfd.c` 对 `RXFRD` storage entry 使用 `RX_DLC_SHIFT=24`，即 DLC 位在 `[27:24]`。`rxf=0x08080404` 按 storage 格式解析得到 DLC=8。

Phase 7.3b 已完成源码侧实现和构建出包：

| 项目 | 结果 |
| --- | --- |
| Kconfig | `CONFIG_SOC_RK3506_CAN_LOOPBACK_PROBE`，默认关闭，sample 显式打开 |
| 目标实例 | CAN0 |
| 模式 | internal loopback + RXSTX + self-test |
| bit timing | 假设 Linux/固件已把 CAN functional clock 配为 300 MHz；当前 NBTP 配置目标为 500 kbit/s |
| TX 帧 | standard ID `0x123`，DLC 8，payload `01 23 45 67 89 ab cd ef` 的 32-bit 小端寄存器写入形式 |
| RX 路径 | internal storage fixed Classical CAN 格式，从 `RXFRD` 读取 frame info / ID / data |
| RXFRD DLC | storage entry 按 Linux `rk3576_canfd.c` 从 `[27:24]` 解析 DLC，不按直接 `RXFRAMEINFO[3:0]` 解析 |
| Linux 对齐 | `INT_MASK=BIT(0)`，启用 `AUTO_RETX_EN | RETX_LIMIT_EN | (0x12c << 3)` |
| 诊断增强 | 打印 `CMD/INT/STATE/STR_STATE/frame_count/left_words/ERROR_CODE/RXERR/TXERR/MODE/RETX` |
| 恢复动作 | 测试后回到 idle mode，清 pending interrupt，恢复进入测试前的 `INT_MASK` |
| 边界 | 不改 pinmux，不要求外部 transceiver，不接 STM32G4，不启用 DMA/CAN-FD |
| 构建 | `PATH=/home/kuro/venv-zephyr-4.4/bin:$PATH ./build.sh amp` 通过 |
| 出包 | `PATH=/home/kuro/venv-zephyr-4.4/bin:$PATH ./build.sh updateimg` 通过 |
| layout | `check_rk3506_zephyr_layout.sh zephyr-rk3506` 通过 |
| 产物 | `update.img` SHA256 `8ff2170157782476d3eec088ea990313edee56f8af30eb0ca1bd3f566892456e` |
| 产物 | `amp.img` SHA256 `f1df0b4472a70345849614b7704ea37c02fe58da7298553fa049a3d10e5f61e3` |
| 产物 | `zephyr.bin` SHA256 `6e02455eda71fc0b7678d0c6a66edfe95eaa51a2c9893a47d7f64e76d9ba89ef` |
| 产物 | `zephyr.elf` SHA256 `185ed135f4674ad71b4ccec2b2c0d1b414894ddb2d2c358887517464af850565` |

Phase 7.3b 板端验证步骤：

1. 烧录 `output/firmware/update.img`。
2. 冷重启。
3. UART 上确认 Phase 7.2 probe 后出现：

```text
[RK3506][CAN-LOOP] phase=7.3b can0 internal loopback; Linux-aligned init; no pinmux/transceiver
[RK3506][CAN-LOOP] armed mode=...
```

4. 成功时应出现：

```text
[RK3506][CAN-LOOP] PASS id=0x123 dlc=8 data=67452301:efcdab89 rxf=0x08080404 ...
[RK3506][CAN-LOOP] restore mode_before=... mode_now=0x00000000 ...
```

5. Linux 侧确认 RPMsg baseline 未被破坏：

```sh
ls -l /sys/bus/rpmsg/devices
/userdata/rk3506_rpmsg_char_ping
```

预期仍然返回：

```text
rx len=6 data="pong:1"
```

如果出现 `CAN-LOOP fail/FAIL/skip`，需要回贴完整 `CAN-PROBE` 和
`CAN-LOOP` 日志，尤其是 `armed` 行和失败行。下一步判断点主要是：

- `skip: can0 clock gate mask`：说明 CAN0 clock gate 未开，先处理 CRU clock/reset。
- `cannot enter idle/work`：说明 `MODE.work_mode` 写入或状态切换不符合预期。
- `timeout` 且 `cmd & TX0_REQ` 仍为 1：TX request 没完成，优先查 mode/bit timing/clock/reset。
- `timeout/no-rx` 且 `TX_FINISH` 已出现但 `frame/left=0`：TX 走了但 internal loopback/RX storage 未入队，优先查 `MODE_RXSTX/LBACK/SELF_TEST` 与 `STR_CTL`。
- `timeout/no-rx` 且 error interrupt 或 `ERROR_CODE/RXERR/TXERR` 非零：按错误类型排查 ACK/bit timing。
- `FAIL rxf/id/data`：说明 loopback 有回包但 fixed storage 解析或 endian/DLC/ID 格式需要修正。

## 9. 下一步

Phase 7.4：外部 Classical CAN 到 STM32G4 或 CAN 分析仪。

进入 Phase 7.4 前必须补齐板级事实：

- OK3506B-S12 实际引出 CAN0 还是 CAN1。
- 是否有板载 CAN transceiver，以及是否需要 STB/EN GPIO。
- CAN pinmux 是否由 Linux/U-Boot 预设，还是需要 Zephyr/RK3506 pinctrl 支持。
- 外部对端的 bitrate/sample point 配置。

## 10. Phase 7.4/7.5：Linux -> RPMsg -> Zephyr -> CAN0 桥接

Phase 7.3b 板端结果：

```text
[RK3506][CAN-LOOP] PASS id=0x123 dlc=8 data=67452301:efcdab89 rxf=0x08080404 ...
```

这证明 CAN0 IP、寄存器初始化、internal storage RXFRD 读取、Classic CAN DLC/ID/data 解析在 CPU2 上已经可用。下一步不是直接写正式 Zephyr CAN driver，而是先做一个板级联调桥：

```text
Linux user tool -> /dev/rpmsg_ctrl0,/dev/rpmsg0 -> Zephyr RPMsg endpoint 0x3003
  -> CAN0 normal mode TX -> STM32G4/FDCAN echo -> CAN0 RX -> RPMsg reply -> Linux
```

### 10.1 当前桥接协议

Linux 发给 Zephyr：

```text
can:txrx:<std_id>:<hex_data>:<timeout_ms>
```

示例：

```text
can:txrx:0x123:1122334455667788:1000
```

约束：

- 只支持 11-bit standard ID，范围 `0x000..0x7ff`。
- 只支持 Classical CAN，payload 0 到 8 字节。
- 默认 bitrate 为 500 kbit/s，当前 NBTP 仍按 CAN functional clock 300 MHz 计算。
- timeout 范围 1 到 10000 ms。

Zephyr 成功回复：

```text
can:rx:id=0x123,dlc=8,data=1122334455667788,rxf=0x...,int=0x...,str=0x...,err=0x...,rxerr=0,txerr=0
```

Zephyr 失败回复：

```text
can:err:ret=-116,int=0x...,str=0x...,err=0x...,rxerr=...,txerr=...
```

常见错误解释：

| 返回 | 含义 |
| --- | --- |
| `can:err:badcmd` | Linux 发来的 RPMsg 字符串格式不合法 |
| `can:err:busy` | Zephyr CAN worker 队列满 |
| `ret=-22` | 参数非法，例如 ID 超范围、payload 超 8 字节、timeout 超范围 |
| `ret=-5` | CAN0 clock gate 或初始化失败 |
| `ret=-116` | 等待 STM32G4 回包超时；优先查收发器、接线、终端电阻、bitrate、STM32 是否 ACK/echo |

### 10.2 当前实现位置

| 组件 | 路径 |
| --- | --- |
| Zephyr CAN0 bridge | `zephyr-rk3506/soc/rockchip/rk3506/rk3506_can_probe.c` |
| Zephyr RPMsg command parser | `zephyr-rk3506/samples/subsys/ipc/rpmsg/rk3506_pingpong/src/rk3506_rpmsg_demo.c` |
| Kconfig gate | `CONFIG_SOC_RK3506_CAN_RPMSG_BRIDGE` |
| OK3506B-S12 CAN0 pinmux gate | `CONFIG_SOC_RK3506_CAN0_RMIO14_16_PINMUX` |
| Linux user tool | `tools/rk3506_rpmsg_char_ping/rk3506_rpmsg_can.c` |
| 内置到 userdata | `device/rockchip/common/extra-parts/userdata/normal/rk3506_rpmsg_can` |

当前 Zephyr 侧会配置 OK3506B-S12/FET3506B-S 外部 CAN0 联调使用的 RMIO 路由：

```text
rm_io14_can0_tx = GPIO0_PB6 mux 7 + RMIO func 0x1c
rm_io16_can0_rx = GPIO0_PC0 mux 7 + RMIO func 0x1d
```

不要继续使用 `rm_io19/rm_io20` 作为 OK3506B-S12 的外部 CAN0 路由：板级表显示
`GPIO0_PC3`/`GPIO0_PC4` 已与 SPI0/RMII 复位或时钟相关功能冲突。

这是板级验证桥接，不是 upstream-ready CAN driver。正式驱动阶段需要迁移到 `drivers/can/`，接入 Zephyr CAN API、DTS pinctrl/clock/reset、IRQ RX、filter API。

### 10.3 板端联调步骤

硬件前提：

- OK3506B-S12 CAN0 TX/RX 接 CAN 收发器。
- STM32G4 FDCAN 也接 CAN 收发器。
- CANH/CANL 正确连接，两端共地。
- 总线两端 120 欧终端电阻。
- STM32G4 配成 Classical CAN 500 kbit/s normal mode，并对收到的帧做 echo。
- Linux 侧不要同时启用/占用同一个 CAN0 控制器。

Linux 侧测试：

```sh
ls -l /sys/bus/rpmsg/devices
/userdata/rk3506_rpmsg_char_ping
/userdata/rk3506_rpmsg_can 0x123 1122334455667788 1000
```

预期：

```text
rx len=... data="can:rx:id=0x123,dlc=8,data=1122334455667788,..."
```

Zephyr UART 侧预期：

```text
[RK3506][CAN-RPMSG] pinmux CAN0 rm_io14/PB6_tx rm_io16/PC0_rx configured
[RK3506][CAN-RPMSG] pinmux regs ... rm_pb6=0x0000001c rm_pc0=0x0000001d
[RK3506][CAN-RPMSG] can0 ready: 500k classic std-id ...
[RK3506][CAN-RPMSG] txrx id=0x123 len=8 -> rx id=0x123 dlc=8
```

如果超时，优先按这个顺序排查：

1. STM32G4 是否真的进入 FDCAN normal mode，是否 ACK，是否 echo。
2. CAN transceiver 供电、STB/EN、CANH/CANL、共地、120 欧终端。
3. 双方 bitrate/sample point 是否一致。
4. OK3506B-S12 CAN0 引脚是否确实对应 `rm_io14/GPIO0_PB6` 与 `rm_io16/GPIO0_PC0`，且 TX/RX 方向没有接反。
5. Linux 是否有 CAN 驱动或其它代码同时操作 CAN0 MMIO。
6. 看 Zephyr 返回的 `err/rxerr/txerr/int/str` 判断是 ACK error、bit error 还是没有 RX 入队。

## 11. Phase 7.6：转入 Zephyr CAN API driver 路径

状态：源码侧已从临时 `RPMsg -> MMIO CAN bridge` 转为 Zephyr CAN API driver
最小实现。当前目标不是完整 upstream-ready CANFD 驱动，而是先把外部 CAN0
主动发包接入 Zephyr 标准 `drivers/can` 设备模型，消除 sample 直接写 CAN0
寄存器的做法。

### 11.1 为什么切换到 Zephyr CAN driver

前面 Phase 7.4/7.5 证明了三件事：

- RPMsg 通路是稳定的，不是当前 CAN 外发问题根因。
- CAN0 IP 本身能工作，internal loopback 已通过。
- 外部总线问题主要来自物理层：收发器 TXD/RXD 接反、外部 STM32 节点残留拉低、
  以及 PB6 板级丝印/复用误判。

因此继续在 RPMsg 命令里绕 MMIO 不再有价值。下一步必须把 CAN 控制器本身做成
Zephyr driver，然后用标准 `can_send()` 验证实际总线。

### 11.2 当前实现范围

| 项目 | 当前状态 |
| --- | --- |
| driver | `zephyr-rk3506/drivers/can/can_rk3506.c` |
| binding | `zephyr-rk3506/dts/bindings/can/rockchip,rk3506-canfd.yaml` |
| Kconfig | `CONFIG_CAN_RK3506` |
| DTS | `&can0 { status = "okay"; bitrate = <500000>; sample-point = <870>; };` |
| chosen | `zephyr,canbus = &can0` |
| app policy | 不使用 sample Kconfig 固化；由 Linux RPMsg 命令决定 |
| 单次 TX 工具 | `/userdata/rk3506_rpmsg_can 0x1b 0101030301000000` |
| 重复 TX 工具 | `/userdata/rk3506_rpmsg_can_tx 0x1b 0101030301000000 1000 1000` |
| Linux 资源归属 | 当前 Zephyr-CAN image 中 Linux `can0` 已在 AMP DTS 中 disabled |

当前驱动能力：

- 支持 Zephyr CAN API 的 `can_start()`、`can_stop()`、`can_set_mode()`、
  `can_send()`、`can_add_rx_filter()`、`can_remove_rx_filter()`、
  `can_get_state()`、`can_get_core_clock()`。
- 支持 Classical CAN standard ID TX/RX，RX 回调经 CAN0 IRQ 和 software filter
  分发。
- 支持 normal、loopback、listen-only、one-shot、triple-sampling 基础模式。
- bit timing 由 DTS 的 `bitrate`/`sample-point` 和 `clock-frequency` 计算，
  已验证 300 MHz functional clock、500 kbit/s、sample-point 870 的 OK3506B-S12
  配置。
- 通过 Zephyr clock/reset/pinctrl API 准备 CAN0 资源，OK3506B-S12 当前实测
  CAN0 路由为：

```text
CAN0_TX = GPIO0_PB6 / RM_IO14 -> transceiver TXD
CAN0_RX = GPIO0_PC0 / RM_IO16 <- transceiver RXD
```

当前限制：

- Phase 7.7 板端验收只覆盖 Classical CAN standard ID、DLC <= 8、normal
  mode、500 kbit/s、STM32G4 echo。
- driver 内部当前使用硬件 accept-all + software filter，尚未把 Zephyr
  filter 完整下沉到 RK3506 acceptance filter 寄存器。
- CAN-FD、BRS、TDC、extended ID、RTR 没有板端验收，不能作为已支持能力宣称。
- DMA 和 external storage mode 未启用。
- Linux RPMsg 用户态 helper 属于 SDK/板级验证工具，不属于 upstream Zephyr
  driver API。

这些限制是有意收敛的：当前先验证真实外部 CAN 总线能由 Zephyr CAN API
稳定主动发包；上层测试策略由 RPMsg 命令承载，不再用 sample Kconfig 固化
应用层 ID、payload、周期或次数。

### 11.3 本轮资源归属

当前 `update.img` 是 Zephyr owns CAN0 的镜像：

- Zephyr CPU2 owns `0xff320000.can`。
- Linux 侧 `OK3506-S-MINI_amp_nand.dts` 中 `&can0` 被覆盖为
  `status = "disabled"`。
- Linux 侧不应再出现 `can0` netdev；这是预期结果，不是错误。

如果需要回到 Linux SocketCAN 测试，应使用 Linux owns CAN0 的镜像，或者移除
AMP DTS 中的 `&can0 { status = "disabled"; };` 覆盖后重打包。不要让 Linux
SocketCAN 和 Zephyr CPU2 同时操作 CAN0。

### 11.4 板端验收步骤

烧录本轮 `output/firmware/update.img` 后冷重启。

Zephyr UART 预期出现 RPMsg 基线日志：

```text
[RK3506][RPMSG] sample main reached
[RK3506][RPMSG] link up
[RK3506][RPMSG] ns announce: rpmsg-ap3-ch0 addr=0x3003
```

从 Linux 触发 CAN TX：

```sh
/userdata/rk3506_rpmsg_can 0x1b 0101030301000000
/userdata/rk3506_rpmsg_can_tx 0x1b 0101030301000000 1000 1000
```

外部 CAN 分析仪或 STM32G4 侧预期：

```text
bitrate: 500 kbit/s Classical CAN
ID:      0x1b
DLC:     8
DATA:    01 01 03 03 01 00 00 00
period: 由 rpmsg_can_tx 的 gap_us/repeat 决定
```

Linux 侧预期：

```sh
ip link show
```

应不再有 `can0`。RPMsg baseline 仍可验证：

```sh
ls -l /sys/bus/rpmsg/devices
/userdata/rk3506_rpmsg_char_ping
```

### 11.5 如果测试失败，按这个顺序判定

1. `/userdata/rk3506_rpmsg_can` 返回 `can:err`：先查 Zephyr 是否烧到新包、
   CAN driver 是否编进 ELF、`zephyr,canbus` 是否指向 `can0`。
2. `ret=-EIO` 或 `txerr` 上升：优先查 ACK、收发器供电、终端电阻、CANH/CANL、
   对端是否在线。
3. CAN 分析仪完全没波形：查 PB6/PC0 到收发器 TXD/RXD 方向、RMIO/IOMUX 寄存器、
   Linux 是否仍占用 CAN0。
4. 有波形但 STM32G4 不动作：确认 ID 是 `0x1b` 还是 `0x11b`，确认 payload
   和 STM32G4 协议一致。
5. 如果电机协议实际要求 `0x11b`，不要改 BSP Kconfig，直接从 Linux 发：

```sh
/userdata/rk3506_rpmsg_can 0x11b 0101030301000000
```

### 11.6 Phase 7.7 验收结果

Phase 7.7 已完成并由板端验证通过。验收链路：

```text
Linux /userdata/rk3506_rpmsg_can
  -> RPMsg endpoint rpmsg-ap3-ch0 / dst 0x3003
  -> Zephyr can_send()
  -> CAN0 PB6/PC0 + transceiver
  -> STM32G4 echo
  -> CAN0 IRQ RX
  -> can_add_rx_filter() callback
  -> RPMsg reply to Linux
```

已确认：

- Linux AMP 镜像中 `can0` disabled，避免 Linux SocketCAN 与 Zephyr CPU2
  同时抢 CAN0 MMIO。
- GIC routing 已把 CAN0 global IRQ 77 路由到 CPU2。
- Zephyr driver 的 TX 完成、RX IRQ、software filter、RPMsg `can:txrx`
  端到端路径可用。
- 本阶段不再重复 GPIO 翻转或 PB6/PC0 纯 pin 测试；该问题已由板端确认。

Phase 7.7 最小复测命令：

```sh
ls -l /sys/bus/rpmsg/devices
/userdata/rk3506_rpmsg_char_ping /dev/rpmsg_ctrl0 ping:can-path 0x3003
/userdata/rk3506_rpmsg_can 0x1b 0101030301000000
```

预期 `rk3506_rpmsg_can` 返回 `can:txrx:ret=0`，且 `rx_id/rx_data`
与 STM32G4 echo 一致。

### 11.7 Phase 7.8：upstream-ready BSP 收敛

Phase 7.8 不再扩大硬件功能范围，目标是把 Phase 7.7 的可用路径整理成
可维护、可审计、接近 Zephyr upstream 形态的 BSP 资产。

本阶段范围：

- 更新 Trellis/current-state/known-good/next-test，删除过期的 `ret=-11`
  失败叙述。
- 增加 CAN API driver test build-only 回归，防止后续重构时丢失 `zephyr,canbus`、
  driver symbol、filter/IRQ API 编译路径。
- 保留 RPMsg CAN 命令作为板级验证入口，但不把 ID、payload、周期等应用
  策略写入 Kconfig。
- 明确已验证能力和未验证能力，避免把 bring-up 成功误写成 CAN-FD/通用
  CAN controller 完工。

Phase 7.8 出口标准：

- `ok3506b_s12_amp_uart1` sample build 通过。
- CAN API driver test build-only 通过或至少能用当前 SDK Zephyr 环境独立构建。
- 文档中明确 Linux owns CAN0 与 Zephyr owns CAN0 两种镜像不能混用。
- 后续 Phase 7.9 才进入 driver 清理：硬件 acceptance filter、CAN-FD、
  timing 覆盖、上游命名/绑定细节和 Twister 覆盖扩展。

### 11.8 Phase 7.9-A：driver API 合规减法

Phase 7.9-A 的目标不是继续扩功能，而是把 `can_rk3506.c` 收窄到已经
板端验证过的 Zephyr CAN API 子集，避免驱动对外宣称未验证能力。

本阶段已执行的减法：

- `get_capabilities()` 只暴露 `CAN_MODE_LOOPBACK`；`CAN_MODE_NORMAL` 为
  Zephyr 的零值基础模式，不需要作为 capability bit 宣称。
- `set_mode()` 只接受 normal/loopback；listen-only、one-shot、
  triple-sampling、CAN-FD mode 均返回 `-ENOTSUP`。
- `send()` 只接受 Classical CAN standard data frame：
  - `flags == 0`
  - standard ID `<= CAN_STD_ID_MASK`
  - DLC `<= CAN_MAX_DLC`
- `add_rx_filter()` 只接受 standard-ID software filter；
  extended-ID filter 数量对外返回 0。
- RX IRQ 路径会 drain 硬件 fixed internal storage，但丢弃 extended/RTR/
  FD/BRS 帧，不向 Zephyr callback 分发未支持帧类型。
- binding 从 `can-fd-controller.yaml` 收窄到 `can-controller.yaml`。
  compatible 仍保留 `rockchip,rk3506-canfd`，因为这是硬件 IP 名称；但
  当前驱动只承诺 Classical CAN。

本阶段保留但未上游化的实现选择：

- 硬件 acceptance filter 仍为 accept-all + Zephyr software filters。
- RX storage 仍使用 Linux 对齐的 fixed internal storage 配置。
- RPMsg CAN helper 仍只是板级验证工具，不是 Zephyr CAN sample API。

Phase 7.9-A 已通过的本地回归：

```sh
./scripts/check_rk3506_zephyr_layout.sh zephyr-rk3506
ninja -C output/zephyr-build/amp2 zephyr/zephyr.bin
west build -b ok3506b_s12_amp_uart1 \
  zephyr-rk3506/tests/drivers/can/api \
  -d output/zephyr-build/rk3506_can_api --pristine=always
twister -T zephyr-rk3506/tests/drivers/can/api \
  -p ok3506b_s12_amp_uart1/rk3506 --build-only -i \
  -A zephyr-rk3506
```

Phase 7.9-A 板端 smoke test：

已通过。板端命令：

```sh
/userdata/rk3506_rpmsg_can 0x1b 0101030301000000
```

用户确认该命令正常，且 Zephyr 侧无 `DATA ABORT`、`FATAL`、kernel panic
或 RPMsg 异常。该结果关闭 Phase 7.9-A：driver API 合规减法后的最小
真实链路没有回退。

当前镜像哈希：

```text
34af5511b1beaff8dfcec3ddad81956a1e077aeb9667b777aafffe3d68258361  output/firmware/update.img
ff9f4f6eda86e6adc343020a32bdfbbc512c86de0bfc93d11791262196883c4b  output/firmware/amp.img
2782185a0ad6bc8dfe065680e60b3edd0607289f7bf00466462586099caa3935  output/zephyr-build/amp2/zephyr/zephyr.bin
9c7e250169132d820b9048f24873d95f638c4a8d5134e826713cf1b8c79029e1  output/zephyr-build/amp2/zephyr/zephyr.elf
```

### 11.9 Phase 7.9-B：driver 行为矩阵和回归收敛

Phase 7.9-B 不改变当前已通过的硬件链路，目标是把 Classical CAN
standard-frame 驱动行为拆成可重复验证的矩阵，避免后续清理时再次靠
临时 RPMsg 日志判断。

本阶段范围：

1. timing：
   - 验证 DTS `bitrate` / `sample-point` 到 NBTP 编码路径；
   - 至少覆盖当前 500 kbit/s、sample-point 870 的板端路径；
   - 非 500 kbit/s 只做 build/API 约束，未上板前不宣称可用。
2. state/error：
   - 明确 `can_get_state()` 在 stopped、error-active、timeout、bus-off
     边界下的返回规则；
   - 把 timeout/error callback 行为写进 CAN API driver test 或后续
     明确分层的专用验证入口。
3. RX/filter：
   - 保持 hardware accept-all + Zephyr software filter；
   - 验证 standard-ID filter 命中、不命中、remove 后不回调；
   - extended-ID filter 继续返回 `-ENOTSUP`。
4. timeout/bus-off：
   - `can_send()` 超时应返回 callback error，不应 panic；
   - bus-off/ACK 错误路径先作为 board-risk 记录，不直接扩大功能。
5. 禁止事项：
   - CAN-FD、extended ID、RTR、listen-only、one-shot、硬件 filter offload
     都必须独立立项验证，不混回当前已验收路径。

Phase 7.9-B 入口本地回归已通过：

```text
layout check: PASS
ninja amp2 zephyr.bin: PASS
west build drivers.can.api.rk3506: PASS
twister drivers.can.api.rk3506 build-only: PASS
```

Twister 入口使用 Zephyr 源码脚本：

```sh
ZEPHYR_EXTRA_MODULES=$PWD/zephyr-rk3506 \
ZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
CROSS_COMPILE=$PWD/prebuilts/gcc/linux-x86/arm/gcc-arm-none-eabi-10-2020-q4-major-x86_64-linux/bin/arm-none-eabi- \
/home/kuro/zephyr-work/zephyr/scripts/twister \
  -G \
  -T zephyr-rk3506/tests/drivers/can/api \
  -p ok3506b_s12_amp_uart1/rk3506 --build-only -i \
  -A zephyr-rk3506 --force-toolchain \
  --outdir output/twister-rk3506-can-api
```

随后已把 `drivers.can.api.rk3506` 扩展为行为矩阵 build-only 验证：

- DTS/API check：
  - `zephyr,canbus` 指向 RK3506 CAN；
  - `clock-frequency = <300000000>`；
  - `bitrate = <500000>`；
  - `sample-point = <870>`；
  - `rx-max-filters > 0`。
- capability/mode：
  - 只允许 normal/loopback；
  - FD、listen-only、one-shot、triple-sampling 返回 `-ENOTSUP`。
- timing：
  - 当前板端 500 kbit/s timing 可设置；
  - odd prescaler / zero TSEG 等非法 timing 返回 `-ENOTSUP`。
- stopped/send：
  - stopped state 可查询；
  - stopped 状态下 valid send 返回 `-ENETDOWN`；
  - RTR/IDE/超范围 standard ID/超 DLC 都被拒绝。
- RX filter：
  - standard exact/masked filter 可分配并可移除；
  - filter slots 用尽后返回 `-ENOSPC`；
  - extended/null/bad id/bad mask filter 被拒绝。

矩阵补充后的本地回归：

```text
layout check: PASS
ninja amp2 zephyr.bin: PASS
west build drivers.can.api.rk3506: PASS
twister drivers.can.api.rk3506 build-only: PASS
```

Twister 报告目录：

```text
output/twister-rk3506-can-api/
```
