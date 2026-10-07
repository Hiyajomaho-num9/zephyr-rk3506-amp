# RK3506 当前状态

更新：2026-10-07（北京时间）。唯一开发根为本 SDK。
本次重新核对源码和交接记录，随后按用户要求重编 SPI 内部回环 AMP、Linux 内核并打包/回拆校验；尚未上板。
目录见 [LAYOUT.md](LAYOUT.md)，命令见 [BUILD.md](BUILD.md)，修改位置见 [CHANGES.md](CHANGES.md)。

## 现在接到哪一步

SDK 单根整理已完成，默认配置是 OK3506-S-MINI NAND + Zephyr AMP，默认应用是 RPMsg pingpong。
2026-09-28 已从这个根目录重新构建 AMP、Linux 6.1.99、五个 Linux 工具并打包 update.img。
2026-10-06 新增双 SPI 内部回环完整包，当前 `output/firmware/` 与此包一致；默认构建应用仍为 pingpong。
SPI0/SPI1/CAN0 时钟持有与 GPIO0_A2 的 PWM 冲突修复已包含在 boot.img 中。
当前主要待办是 Linux GIC 的 AMP 中断保护，以及新组合的板上复验。

| 内容 | 已有证据 | 当前边界 |
|---|---|---|
| 单根源码、构建路径、Git 迁移 | 9/28 构建通过，10/06–10/07 结构检查通过；本轮分两笔提交 | 供应商基线不整体导入 Git |
| U-Boot AMP、原 GPIO 驱动 | 用户此前确认已经烧写/上板可用 | 不等于 9 月新固件与新引脚测试已验收 |
| RPMsg/private cache | 旧记录有已验证基线；9/28 pingpong 编译通过 | GIC-safe 新版与新 Linux 组合仍缺上板结果；ping 有轮询兜底 |
| SPI0/SPI1 | 内部/SPI1 引脚配置此前编译通过；10/06 重编内部回环并交付完整包 | 纯轮询，无 IRQ/DMA；两种回环均待上板 |
| GPIO3_A6/A7 API、GPIO shell | 安全引脚与 IRQ group 3 配置已编译通过 | 新引脚和中断路径仍待复验 |
| CAN0 | 历史报告记录 STM32G4 外部 TX/RX/filter/IRQ 通过，驱动精简后回归通过；9/28 pingpong 包含 CAN | 历史范围是 Classical CAN 标准帧、500 kbit/s；新 GIC/cache/Linux 组合需复验 |
| Linux 时钟/PWM 修复 | 源码及归档 DTB：12 个时钟，PWM0_CH1 disabled | 板上当前烧的版本未知 |
| Twister | 10/06 确认 Python 环境缺 natsort、ply、pytest | 当前五目标 SDK 构建不能代替 Twister 结果 |

## 前面的工作时间线

| 日期 | 实际停点 |
|---|---|
| 早期基线 | RPMsg/cache 长跑、CAN0 与 STM32G4 外部链路已有验收记录；CAN Phase 7.9-B 行为矩阵进入 build-only 回归 |
| 9/04 | 本轮整理前最后一次 Git 提交 `cbb9882a627`：GPIO power-cycle 应用 |
| 9/23–9/24 | MMU/cache 守卫与 GIC_SAFE_CONFIG 修正，GIC-safe pingpong/powercycle 编译归档 |
| 9/25 | Claude 完成 SPI 整合和引脚审核；GPIO 环回改为 GPIO3_A6/A7；尚未提交和新增板上验收 |
| 9/28 | Codex 核对交接，编译完整升级包，迁入唯一 SDK 根并重新构建/回拆验证 |
| 10/06 | 整理当前文档，重编 SPI 内部回环 AMP/内核并出完整包；配置、回拆、分区和哈希检查通过 |
| 10/07 | 按用户要求整理源码提交并同步 GitHub；SPI 完整镜像按测试版 Release 交付 |

Claude 原始会话最后实质性总结为 `2026-09-25T14:44:15.838Z`（北京时间 22:44）。
下一条用户消息只是询问是否仍在，未找到新增上板反馈。
可追溯摘要见 [原交接](history/external-repo-ZCODE-SYNC.md) §15。
CAN 历史验收见 [CAN bring-up notes](../../zephyr/doc/rk3506/can/RK3506_CAN_BRINGUP_NOTES.md) §11.6–§11.9；
当时也记录了 CAN API/Twister build-only 通过，但该环境和旧路径不能直接代表当前 SDK 根与 Python 环境。

## 当前镜像与验证证据

最新完整包：`output/releases/update-20261006-020523Z-spi-internal/update.img`。
GitHub 下载：[rk3506-spi-internal-20261006](https://github.com/Hiyajomaho-num9/zephyr-rk3506-amp/releases/tag/rk3506-spi-internal-20261006)。
AMP 为 SPI0/SPI1 内部回环，不需跳线，不创建 RPMsg 端点。
大小：71,819,850 字节。SHA-256：

```text
79165572a9280bd39204b9a77f6efb2fde84bac9ae5c881ea84b2d3441928ebf
```

10/06 构建与回拆日志在 `output/validation/update-20261006-020523Z-spi-internal/`。
9 个打包项逐字节比对通过，固定分区大小检查通过，六项 SHA256SUMS 通过；
AMP payload 与本次 zephyr.bin 一致，boot DTB 与新编 DTB 一致，仍有 12 个 AMP 时钟和 PWM0_CH1 disabled。
实际配置确认 SPI 开启、RPMsg 关闭，两个 SPI 节点没有 pad pinctrl。
归档 `manifest.json` 标记 `hardware_tested: false`。
完整包包含 userdata；U-Boot、loader、rootfs、oem、userdata 沿用既有分区产物。

此前 pingpong 包保留于 `output/releases/update-20260928-025357Z-sdk-root/`，其 SHA256SUMS 也已复核通过。
其他测试固件见 [测试对照](testing/README.md)。

9/28 本机验证在 `output/validation/sdk-layout-20260928-024546Z/`：

- `results.json` 记录五种 AMP 配置、内核、升级包和 Linux 工具重试通过。
- 故意指定不存在应用返回 1，验证 SDK 正确传播构建失败。
- Linux 工具首次失败后补齐旧 UAPI 的 `RPMSG_ADDR_ANY` 定义，重试通过；失败记录保留。
- 归档 manifest 记录 9 个打包项回拆哈希一致，固定分区大小检查通过。
- 编译日志仍有已知 `__rbit` 等告警；不能称为零告警或硬件验收。

## 下一步顺序

1. 修复 Linux GIC 对 AMP IRQ 的初始化保护。
   `kernel-6.1/drivers/irqchip/irq-gic.c:gic_dist_init()` 调用
   `irq-gic-common.c:gic_dist_config()`，后者仍对所有 SPI 写 ACTIVE_CLEAR/ENABLE_CLEAR，未排除 AMP IRQ。
   补丁要核对 AMP 的使能、触发方式和路由保留，再编译内核并复验；当前先保留为待办。
2. 用明确版本的 boot/amp 验证时钟/PWM 修复、SPI 两种回环，再验 GPIO 中断及 CAN 外部收发。
   同时回归 RPMsg；pingpong 有轮询兜底，ping 通不足以证明 mailbox IRQ 正常。
3. 把 GPIO 默认保留范围下沉到板级 DTS。当前测试 overlay 自行约束，应用漏写时仍可能碰 Linux 引脚。
   GPIO2 在 NAND 板上用于 NAND/RMII；GPIO3 测试只使用 A6/A7（SoM 129/128），中断使用 group 3。
4. 补齐 Twister 环境并处理构建告警，再整理提交。mailbox 标准 MBOX 化等上游化工作排在功能验收之后。

## Git 与旧资料

镜像构建时 HEAD 为 `cbb9882a6273687b09d1cb462b43722a3e11659e`，当时包含未提交源码，
该事实保留在构建 manifest 中。10/07 将源码整理为驱动/BSP 与 SDK 迁移两笔提交。
当前 HEAD 用 `git log -1` 查看；SPI 编译后没有追加驱动逻辑修改。
本次推送前获取了 origin/main，确认远端无新增提交，使用正常快进推送。
Linux 内置 `.gitignore` 放行的少量厂商元文件不纳入本轮提交。
修改分组和 Git 边界见 [CHANGES.md](CHANGES.md)。

旧记录位于 [history/](history/README.md)，Zephyr 树内的早期报告见
[历史报告说明](../../zephyr/doc/rk3506/README.md)。旧路径、旧阶段号和历史测试结果不代表当前镜像状态。
