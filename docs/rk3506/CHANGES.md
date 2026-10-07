# RK3506 本轮改动地图

更新：2026-10-07。本页按实际文件位置说明 9 月至 10 月的修改。
本轮从 `cbb9882a627` 后整理为驱动/BSP 与 SDK 单根迁移两笔提交，用 `git log` 查看具体版本。
当前本地未提交状态用 `git status --short -uall` 查看；状态与验收见 [STATUS.md](STATUS.md)。

| 分组 | 修改位置 | 内容及验证边界 |
|---|---|---|
| 目录迁移 | 根 README、`.gitignore`、`docs/rk3506/`、SDK 原生 `device/`、`kernel-6.1/`、`tools/` | 原 `linux/...` overlay 与外部同步流程退役；Git 历史保留，旧路径的删除需与新路径一起审阅 |
| 构建胶水 | `device/rockchip/.chips/ok3506/amp-zephyr.cfg`、`amp_linux_zephyr.its`、`device/rockchip/common/scripts/{build,mk-amp}.sh` | 路径只定义一次、模块取本树、按源码构建、overlay 转发、错误码传播；9/28 五 AMP 配置通过 |
| Linux AMP 资源 | `kernel-6.1/arch/arm/boot/dts/OK3506-S-MINI_amp_nand.dts`、同目录 common/AMP DTS | SPI/CAN 时钟持有、PWM0_CH1 禁用、资源划分；已进入 boot.img，GIC 保护尚未实现 |
| MMU/cache 与 GIC-safe | `zephyr/arch/arm/core/mmu/arm_mmu.c`、`zephyr/soc/rockchip/rk3506/{soc.c,Kconfig,Kconfig.defconfig,rk3506_rpmsg_platform.c}` | cache 决策归 SoC，避免关闭 D-cache 时丢失数据；启用 GIC_SAFE_CONFIG；新组合待硬件复验 |
| GPIO、pinctrl、CRU | `zephyr/drivers/{gpio,pinctrl,clock_control}/`、对应 bindings、dt-bindings、RK3506 DTS | GPIO/pinmux/电气属性和时钟实现调整；原 GPIO 曾上板，新覆盖范围不能都算已验证 |
| SPI | `zephyr/drivers/spi/`、SPI binding、SoC MMU/DTS、`zephyr/samples/rk3506/spi_loopback/` | 控制器轮询驱动、内部/外部回环测试；两种配置编译通过，待上板 |
| GPIO 应用与测试 | `zephyr/samples/rk3506/gpio_power_cycle/`、GPIO API 的板级 overlay、`zephyr/boards/forlinx/ok3506b_s12/debug/` | power-cycle 改为单次按键时序；API/shell 限定 GPIO3_A6/A7，GPIO2 调试 overlay 屏蔽全部引脚 |
| Linux 工具 | `tools/rk3506_rpmsg_char_ping/`、`mk-rk3506-tools.sh`、对应 build hook | 五个工具输出到 `output/tools/`；兼容旧 UAPI，stress 工具调整保留；不自动装入 rootfs |
| 当前文档 | `docs/rk3506/`、SPI 样例 README、根 README | 构建入口、状态、证据和历史划分；10/06 再次整理 |

## 提交与 Git 边界

- 驱动/BSP 提交保留迁移前的开发修改，包含 SPI、GPIO/pinctrl/CRU、cache/GIC-safe 和安全 GPIO 测试。
- SDK 迁移提交同时记录旧 `linux/...`、`boards/debug/...` 路径退役及原生新路径、构建胶水和当前文档。
- `kernel-6.1/.clang-format` 等少量厂商元文件会因 Linux 自带的忽略例外而出现，本轮未把这些文件纳入提交。
- `output/` 与 `archive/` 不纳入源码提交。供应商基线未整体导入，其他机器仍需对应 SDK 基线。

迁移前备份：`archive/layout-before-20260928-023401Z/`；其中保留原 Git、未提交源码、patch 和映射。
完整 SPI 测试镜像作为 GitHub Release 附件交付，二进制不写入源码历史。
每个功能提交及测试版均注明构建证据，新增固件的板上验收状态继续单独维护。
