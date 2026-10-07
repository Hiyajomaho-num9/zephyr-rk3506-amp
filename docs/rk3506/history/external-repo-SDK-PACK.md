# SDK 打包指南 — 从本仓库生成 amp.img / update.img

> 面向编译机（10.0.0.117）与桌面机的通用流程。2026-09-03 起打包线固件源
> 已切换到本仓库（zephyr-rk3506-amp），旧 SDK 内嵌工程 `zephyr-rk3506/`
> 与已删除的 `~/zephyr-work/zephyr` 不再使用。

## 前提（一次性）

1. SDK 路径：`~/rk3506/OK3506B-S12_Linux6.1.99/SDK/OK3506_Linux_Source`
2. SDK 内建立指向本仓库的 symlink（打包线所有路径经它解析）：

   ```sh
   ln -sfn ~/my_code/zephyr-rk3506-amp \
     ~/rk3506/OK3506B-S12_Linux6.1.99/SDK/OK3506_Linux_Source/zephyr-rk3506-amp-link
   ```

3. `device/rockchip/.chips/ok3506/amp_linux_zephyr.its` 的 `compile` 块与
   `device/rockchip/.chips/ok3506/amp-zephyr.cfg` 已指向
   `zephyr-rk3506-amp-link/...`（2026-09-03 完成，勿改回旧路径）。
4. `device/rockchip/common/scripts/mk-amp.sh` 已打补丁：支持
   `RK_AMP_ZEPHYR_EXTRA_MODULES` 环境变量（见下）。
5. python ≥3.12：`~/venv-zephyr-py312`（桌面机侧用等价 venv 即可）。

## 日常打包

```sh
export PATH=$HOME/venv-zephyr-py312/bin:$HOME/.local/bin:$PATH
cd ~/rk3506/OK3506B-S12_Linux6.1.99/SDK/OK3506_Linux_Source
Python3_EXECUTABLE=$HOME/venv-zephyr-py312/bin/python ./build.sh amp        # 出 amp.img
Python3_EXECUTABLE=$HOME/venv-zephyr-py312/bin/python ./build.sh updateimg  # 出可烧录 update.img
sha256sum output/firmware/amp.img out/zephyr.bin
```

产物：

- `output/firmware/amp.img` — AMP 分区镜像（FIT，load 0x03e00000）
- `output/update/Image/update.img` — 整包烧录镜像
- `out/zephyr.bin` — 本仓库内 prebuilt 缓存（git 忽略），打包后自动刷新

## 增量语义

- 源码（samples/boards/soc/dts）比 `out/zephyr.bin` 新 → 自动重建
- 无变化 → 快路径直接拷缓存（秒级）
- 强制全量：`./build.sh clean-amp && ./build.sh amp`

## 可覆盖变量（跨机器复用）

所有路径经由 `amp-zephyr.cfg`，均可被环境变量覆盖：

| 变量 | 默认 | 说明 |
|---|---|---|
| `RK_AMP_ZEPHYR_TREE` | `$SDK/zephyr-rk3506-amp-link` | 本仓库根（symlink 或真实路径）|
| `RK_AMP_ZEPHYR_APP` | `$TREE/zephyr/samples/.../rk3506_pingpong` | 应用目录 |
| `RK_AMP_ZEPHYR_BOARD` | `ok3506b_s12_amp_uart1` | 板型 |
| `RK_AMP_ZEPHYR_DIR` | `$TREE/zephyr` | ZEPHYR_BASE（subtree 自包含）|
| `RK_AMP_ZEPHYR_PREBUILT_BIN` | `$TREE/out/zephyr.bin` | prebuilt 缓存 |
| `RK_AMP_ZEPHYR_EXTRA_MODULES` | cmsis + picolibc（树内拷贝）| 分号分隔 |

例：桌面机上从另一份 checkout 打包：

```sh
RK_AMP_ZEPHYR_TREE=/d/work/zephyr-rk3506-amp ./build.sh amp
```

## 重要：为什么必须传 EXTRA_MODULES

本仓库 west workspace（`~/my_code`）已不再提供 picolibc/cmsis 的构建路径；
不带 `RK_AMP_ZEPHYR_EXTRA_MODULES` 时 Kconfig 会静默退回 minimal libc
（`CONFIG_MINIMAL_LIBC=y`），与仓库基准配置（`CONFIG_PICOLIBC=y`）不一致，
printf/malloc 行为改变。cfg 已默认传入树内 `external_modules/` 拷贝。

## 已验证（2026-09-03）

- 全量重建 → `amp.img`（95232B，FIT 内嵌 zephyr.bin sha256 `d1f50765…`）
- prebuilt 快路径 → 产物一致
- touch 源文件 → 自动重建，缓存刷新

## SDK 侧文件的版本化（2026-09-23）

上面"前提"第 2-4 条改的都是 SDK 树里的文件，SDK 重新解包 / 重新同步会把它们静默抹掉。
这些文件现在都有仓库内镜像，由 `scripts/sdk-sync.sh` 维护：

| 仓库路径 | SDK 路径 |
|---|---|
| `sdk/device/rockchip/.chips/ok3506/amp_linux_zephyr.its` | `device/rockchip/.chips/ok3506/amp_linux_zephyr.its` |
| `sdk/device/rockchip/.chips/ok3506/amp-zephyr.cfg` | `device/rockchip/.chips/ok3506/amp-zephyr.cfg` |
| `sdk/device/rockchip/common/scripts/mk-amp.sh` | `device/rockchip/common/scripts/mk-amp.sh` |
| `linux/kernel-6.1/arch/arm/boot/dts/OK3506-S-MINI-common.dtsi` | `kernel-6.1/arch/arm/boot/dts/OK3506-S-MINI-common.dtsi` |
| `linux/kernel-6.1/arch/arm/boot/dts/OK3506-S-MINI_amp_nand.dts` | `kernel-6.1/arch/arm/boot/dts/OK3506-S-MINI_amp_nand.dts` |
| `linux/kernel-6.1/arch/arm/boot/dts/OK3506-S-MINI_linux_nand.dts` | `kernel-6.1/arch/arm/boot/dts/OK3506-S-MINI_linux_nand.dts` |
| `linux/kernel-6.1/arch/arm/boot/dts/rk3506-amp.dtsi` | `kernel-6.1/arch/arm/boot/dts/rk3506-amp.dtsi` |

```sh
scripts/sdk-sync.sh status    # 逐文件 same / DIFFERENT / missing，并检查 zephyr-rk3506-amp-link
scripts/sdk-sync.sh diff      # 看具体差异（仓库副本 vs SDK 副本）
scripts/sdk-sync.sh install   # SDK 重同步之后：仓库 → SDK，并重建 symlink
scripts/sdk-sync.sh import    # 在 SDK 里改过之后：SDK → 仓库，然后提交
```

SDK 路径默认取 `$RK_SDK_DIR`，否则用编译机路径；也可作为第二个参数传入。
`OK3506-S-MINI-common.dtsi` 里 `&spi0` / `&spi1` 被禁用是 CAN0 引脚冲突的权宜之计
（RM_IO16/GPIO0_C0、RM_IO14/GPIO0_B6），不是 AMP 需要，改动前先看 ZCODE-SYNC.md §14。

## SDK 路径兼容与产物校验（2026-09-28）

`mk-amp.sh` 现在通过 `realpath -m` 规范化 Zephyr 应用、源码根、BOARD/SOC/DTS_ROOT、
构建目录和缓存路径。SDK 的 `zephyr-rk3506-amp-link` 入口仍保留，但传给 CMake 的相关路径
使用同一套真实路径，避免同一份 CMake 文件被不同路径重复加载而产生递归。
pingpong 是完整 Zephyr 树内的应用，不能把 Zephyr 根目录追加为外部模块；该旧自注册已移除。

2026-09-28 已按当前 NAND AMP 配置生成完整 update.img，详见 `ZCODE-SYNC.md` §16。
验证必须检查新产物存在及其内容：SDK 主 build.sh 的既有错误处理可能在 hook 失败后仍返回 0。
本次通过 FIT 提取校验和 rkImageMaker / afptool 回拆逐项哈希核对确认产物，而非只检查退出码。
