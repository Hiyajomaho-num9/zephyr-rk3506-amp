# RK3506 用 Zephyr 完整替换 RT-Thread AMP 方案设计

> Historical note, 2026-05-16:
> this was the early replacement plan.  The active implementation has since
> moved to the Zephyr-style out-of-tree tree `zephyr-rk3506/`.  Do not use the
> old top-level staging paths in this document for new work.

- 日期：2026-04-21
- 项目根目录：`/home/kuro/rk3506/OK3506B-S12_Linux6.1.99/SDK/OK3506_Linux_Source`
- 当前状态：U-Boot AMP 启动链已打通，RT-Thread 当前可正常作为 `amp2` 固件启动
- 目标：规划如何用 Zephyr 最终替换当前 RT-Thread AMP 固件方案

## 0. 当前已落地的第一阶段骨架

本轮已在 SDK 内新增一套并行的 Zephyr AMP 骨架，且不覆盖现有 RT-Thread 方案：

- 新顶层产品配置：
  - `device/rockchip/.chips/ok3506/OK3506-S-MINI_amp_nand_zephyr_defconfig`
- 新 AMP FIT：
  - `device/rockchip/.chips/ok3506/amp_linux_zephyr.its`
- 新 Zephyr 默认参数：
  - `device/rockchip/.chips/ok3506/amp-zephyr.cfg`
- `mk-amp.sh` 已增加：
  - `sys = "zephyr"` 的最小构建/打包分支
  - 支持优先使用预编译 `zephyr/out/zephyr.bin`
  - 支持在设置 `RK_AMP_ZEPHYR_DIR`/`ZEPHYR_BASE` 后尝试源码构建
- 新增 SDK 内实验目录：
  - `zephyr/README.md`
  - `zephyr/apps/amp_uart1/`
  - `zephyr/boards/arm/ok3506_amp_uart1/`

这意味着现在已经具备：

1. 并行选择 Zephyr AMP 配置的入口
2. 将 `zephyr.bin` 打进 `amp.img` 的通路
3. 一个最小 Zephyr AMP 应用骨架
4. 一个可被 Zephyr 硬件枚举脚本识别的 `ok3506_amp_uart1` board / `rk3506` soc 骨架

但仍未具备：

- RK3506 Zephyr SoC/board 完整 BSP
- RPMsg/OpenAMP 通信适配
- UART1 时钟和 pinmux 的最终实机验证

## 1. 目标定义

“完整替换 RT-Thread AMP 方案”这里的完整，建议拆成两层：

### 第一层：运行替换

目标：

- 保持当前 U-Boot AMP 启动链不变
- 把 `amp2` 的数据体从 `rtt2.bin` 改成 `zephyr.bin`
- 让 CPU2 在 `0x03e00000` 跑起 Zephyr
- UART1 打印 Zephyr 日志

这是最小可验证目标。

### 第二层：能力替换

目标：

- 用 Zephyr 替代当前 RT-Thread 在 RK3506 AMP 中承担的远端固件角色
- 最终和 Linux 侧已有的 reserved-memory / RPMsg / mailbox 框架对接

这是完整替换目标。

## 2. 当前已知前提

### 2.1 U-Boot AMP loader 的通用性已经确认

根据当前 SDK 和 Luckyfox U-Boot 代码，U-Boot proper 启动 AMP 的条件是：

- `amp` 分区存在
- `amp.img` 是合法 FIT
- `conf` 下有 `loadables`
- 子镜像节点里有：
  - `cpu`
  - `load`
  - `arch`
  - `thumb`
  - `hyp`
- 数据体是一个可执行固件二进制

换句话说，U-Boot proper 并不关心远端固件是不是 RT-Thread。

### 2.2 当前内存和资源布局

当前 Linux DTS 中已固定的 AMP 相关布局：

- `amp-shmem@3b00000` -> `0x03b00000 ~ 0x03c00000`
- `rpmsg@3c00000` -> `0x03c00000 ~ 0x03d00000`
- `rpmsg-dma@3d00000` -> `0x03d00000 ~ 0x03e00000`
- `amp@3e00000` -> `0x03e00000 ~ 0x03f00000`
- `mcu@fff80000` -> `0xfff80000 ~ 0xfff8c000`

当前 `amp2` 使用：

- `cpu = <0xf02>`
- `load = <0x03e00000>`
- `srambase = <0xfff80000>`
- `sramsize = <0x0000c000>`

### 2.3 当前 RT-Thread 关键功能点

当前 RT-Thread 固件至少已经验证了：

- Cortex-A7 AArch32 作为 AMP 远端可运行
- UART1 RMIO 可作为控制台
- 入口地址 `0x03e00000` 可行
- U-Boot 可通过 `psci_cpu_on()` 启动 CPU2

这说明板级硬件启动路径已具备被 Zephyr 复用的基础。

## 3. Zephyr 替换方案的总体思路

建议不要一开始就“替换全部能力”，而是分阶段推进。

### Phase 0：不动当前稳定 RT-Thread 方案

保留：

- 当前 `OK3506-S-MINI_amp_nand_defconfig`
- 当前 `amp_linux.its`
- 当前 `ok3506_amp_uart1_defconfig`

目的：

- 作为随时可回退的稳定基线

### Phase 1：引入 Zephyr 作为新的 `amp2` 固件

新增一个与现有 RT-Thread 并行的 Zephyr 方案，不直接覆盖当前工作流。

建议新增：

- 新 ITS，例如：`amp_linux_zephyr.its`
- 新顶层 defconfig，例如：`OK3506-S-MINI_amp_nand_zephyr_defconfig`

好处：

- 不破坏现在已经打通的 RT-Thread 流程
- 方便对比两个方案
- 出现问题时回退简单

### Phase 2：先让 Zephyr 在 CPU2 上最小跑通

只做：

- Cortex-A7 单核启动
- UART1 输出
- 基本 timer / interrupt

先不碰：

- RPMsg
- mailbox
- OpenAMP

### Phase 3：让 Zephyr 接入当前 AMP 通信资源

补：

- shared-memory
- vrings
- mailbox transport
- RPMsg/OpenAMP backend

### Phase 4：最终切换默认 AMP 远端固件

在前 3 阶段稳定后，再考虑是否：

- 让 Zephyr 取代 RT-Thread 成为默认 `amp2`

## 4. 推荐目录与集成方式

## 4.1 不建议的方式

不建议一开始就强行把 Zephyr 塞进当前 `rtos/` 目录结构里。

原因：

- 当前 `mk-amp.sh` 明确只支持 `rtt` / `hal`
- `rtos/` 的构建流是围绕 SCons 和 RT-Thread BSP 组织的
- 直接硬改会让回退成本很高

## 4.2 推荐方式

推荐新增单独目录，例如：

```text
zephyr/
  zephyr-app/
  boards/
  soc/
  modules/   (如有需要)
```

或者：

```text
external/zephyr-rk3506/
```

二选一都可以，但建议：

- SDK 内留一层自己的工程壳
- Zephyr 主仓可以作为子目录/子模块/外部依赖

## 4.3 推荐的第一阶段集成方式

先不要急着改 `mk-amp.sh` 支持所有 Zephyr 语义。

第一阶段可以分两步：

### 方式 A：手动替换验证

1. 单独把 Zephyr 编出 `zephyr.bin`
2. 手工复制/链接成 `rtt2.bin` 或修改 ITS 指向 `zephyr.bin`
3. 复用当前 `amp2` 启动链验证

优点：

- 变化最小
- 最快验证 U-Boot 对 Zephyr 固件是否无感

### 方式 B：再做正式集成

验证成功后，再扩展 `mk-amp.sh`：

- 增加 `build_zephyr()`
- 支持 `sys = "zephyr"`
- 支持 Zephyr board/target 配置

## 5. 需要新增或修改的关键文件

## 5.1 SDK 侧

建议新增：

- `device/rockchip/.chips/ok3506/OK3506-S-MINI_amp_nand_zephyr_defconfig`
- `device/rockchip/.chips/ok3506/amp_linux_zephyr.its`
- `rk3506-zephyr-replacement-plan.md`（本文件）

可能修改：

- `device/rockchip/common/scripts/mk-amp.sh`

如果做正式集成，还可能新增：

- `device/rockchip/common/scripts/mk-zephyr.sh`

## 5.2 Zephyr 工程侧

建议新增一个 RK3506 的最小 Zephyr 平台端口，至少包含：

- Board DTS
- Board Kconfig/defconfig
- SoC Kconfig
- Linker/memory 布局
- UART1 控制台配置

建议命名方向：

- board: `ok3506_amp_uart1`
- soc/family: `rockchip/rk3506`

## 5.3 Linux 侧

第一阶段尽量不改 Linux DTS。

因为当前 Linux 侧这几项已经验证正确：

- reserved-memory
- `rockchip_amp`
- `rockchip-rpmsg`
- UART1 IRQ route

只有进入 RPMsg/OpenAMP 阶段，才再评估 Linux 侧是否需要调整。

## 6. Zephyr 最小 bring-up 所需能力

## 6.1 必需项

### 1）CPU 架构

Zephyr 已具备：

- AArch32 Cortex-A7 架构支持

需要在板级/SoC 侧正确选择：

- `CPU_CORTEX_A7`
- ARMv7-A

### 2）链接地址

Zephyr 镜像必须能链接到：

- `0x03e00000`

并且不能覆盖：

- `rpmsg`
- `rpmsg-dma`
- `amp-shmem`

### 3）控制台

第一阶段一定要先通：

- UART1
- 1500000 波特率

这是最直接的 bring-up 验证通道。

### 4）基本中断控制器

至少要能跑：

- GIC
- 基本 IRQ 初始化

### 5）基本定时器

至少要能驱动：

- 系统 tick
- 延时

## 6.2 可后置项

这些先不要一开始就做：

- RPMsg
- mailbox transport
- OpenAMP
- 文件系统
- 多线程复杂组件

## 7. 当前最大的技术风险

## 7.1 RK3506 没有现成 Zephyr BSP

这不是小风险，是当前最大的工作量来源。

意味着你需要自己补：

- board
- soc
- 启动布局
- 部分驱动 glue

## 7.2 Rockchip mailbox / RPMsg transport 没有现成支持

Zephyr 有：

- OpenAMP
- RPMsg

但当前没有现成 RK3506 / Rockchip mailbox 后端。

所以“让 Zephyr 跑起来”和“让 Zephyr 复用当前 Linux `rockchip-rpmsg`”是两件不同难度的事。

## 7.3 当前 U-Boot AMP 启动约束固定

当前 U-Boot 已固定使用：

- `cpu = <0xf02>`
- `load = <0x03e00000>`

所以 Zephyr 端必须适应这套约束，而不是反过来改启动链。

## 8. 最小实施路线

## Milestone 1：Zephyr hello 替代 RTT

目标：

- 在 CPU2 上运行 Zephyr
- UART1 打印

建议步骤：

1. 建最小 Zephyr board/SoC 工程
2. 固定链接地址到 `0x03e00000`
3. 只启 UART1 和最小内核
4. 产出 `zephyr.bin`
5. 手工替换 `amp2` 数据体
6. 验证 U-Boot 是否成功 bring-up CPU2

通过标准：

- U-Boot 仍打印 `AMP: Brought up cpu[f02] ... OK`
- UART1 出现 Zephyr banner 或 hello

## Milestone 2：SDK 编译集成

目标：

- 不靠手工拷贝，直接由 SDK 构建出 Zephyr AMP 镜像

建议步骤：

1. 新增 `amp_linux_zephyr.its`
2. 扩展 `mk-amp.sh` 或新增 `mk-zephyr.sh`
3. 让 `./build.sh amp` 可直接产出 Zephyr 版 `amp.img`

通过标准：

- `./build.sh amp` 直接生成 Zephyr 远端固件

## Milestone 3：最小共享内存通信

目标：

- 先证明 Zephyr 能访问当前预留共享内存

建议步骤：

1. 将当前 reserved-memory 区域映射到 Zephyr 侧
2. 做最小共享内存读写验证
3. 再考虑 mailbox 触发

## Milestone 4：RPMsg / OpenAMP

目标：

- 与 Linux 侧 `rockchip-rpmsg` 对接

建议步骤：

1. 对照当前 Linux rpmsg 节点：
   - `link-id`
   - mailbox
   - vring 区域
2. 做 Rockchip mailbox shim
3. 接 Zephyr 的 OpenAMP / RPMsg 后端

## 9. 建议的实施文件清单

如果正式开始实现，建议优先创建这些文件或目录：

```text
zephyr/
  app/
  boards/rockchip/ok3506_amp_uart1/
  soc/rockchip/rk3506/

device/rockchip/.chips/ok3506/
  OK3506-S-MINI_amp_nand_zephyr_defconfig
  amp_linux_zephyr.its
```

以及视实现方式可能新增：

```text
device/rockchip/common/scripts/mk-zephyr.sh
```

## 10. 当前建议

结论非常明确：

### 建议立刻开始做的

- **不是 RPMsg**
- **不是 Linux 侧改动**
- **而是先做 Zephyr 最小 bring-up**

也就是：

> 先让 `amp2` 从 `rtt2.bin` 变成 `zephyr.bin`，并在 CPU2 上通过 UART1 打印出来。

### 只有这一阶段通了，才值得继续做完整替换。

## 11. 下一步建议执行项

下一轮建议直接进入下面两件事：

1. 设计 `ok3506_amp_uart1` 的 Zephyr 最小 board/SoC 结构
2. 选定第一版集成策略：
   - 手动替换 `amp2` 数据体
   - 还是直接扩展 `mk-amp.sh`

当前建议优先选：

- **手动替换验证**

因为它能最快回答一个最关键的问题：

> **Zephyr 固件在 RK3506 当前 U-Boot AMP loader 下是否能直接跑起来。**
