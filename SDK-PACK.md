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
