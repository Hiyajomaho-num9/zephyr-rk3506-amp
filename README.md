# RK3506 Linux + Zephyr AMP

本目录是唯一开发根目录，Git 工作树也位于这里。

- `zephyr/`：实际 Zephyr 源码、RK3506 BSP、驱动、应用和测试。
- `kernel-6.1/`：实际 Linux 源码及 AMP DTS。
- `device/rockchip/`：唯一板级配置和构建脚本。
- `tools/rk3506_rpmsg_char_ping/`：Linux RPMsg/CAN 工具源码。
- `docs/rk3506/`：构建、测试、状态和历史记录。
- `output/`：构建产物，`output/releases/`：镜像归档。
- `archive/`：迁移前备份和废弃工程，不参与构建。

## 开始开发

```sh
export PATH="$HOME/venv-zephyr-py312/bin:$HOME/.local/bin:$PATH"
export Python3_EXECUTABLE="$HOME/venv-zephyr-py312/bin/python"
./scripts/check_rk3506_zephyr_layout.sh
./build.sh amp
./build.sh kernel
./build.sh updateimg
```

整包在 `output/firmware/update.img`；AMP 镜像在 `output/firmware/amp.img`。
Linux RPMsg/CAN 工具使用 `./build.sh rk3506-tools`，输出在 `output/tools/rk3506_rpmsg_char_ping/`。

最新 SPI 内部回环完整镜像已重编并回拆校验：
`output/releases/update-20261006-020523Z-spi-internal/update.img`，校验值见同目录 `SHA256SUMS`。
GitHub 下载入口：[SPI 内部回环测试版](https://github.com/Hiyajomaho-num9/zephyr-rk3506-amp/releases/tag/rk3506-spi-internal-20261006)。
此前 pingpong 完整包仍在 `output/releases/update-20260928-025357Z-sdk-root/`。
默认构建应用仍为 pingpong；当前 `output/firmware/` 是最近一次构建的 SPI 测试产物。

## 文档入口

- [目录和责任边界](docs/rk3506/LAYOUT.md)
- [构建、切换应用和调试配置](docs/rk3506/BUILD.md)
- [当前进度和待办](docs/rk3506/STATUS.md)
- [本轮改动地图](docs/rk3506/CHANGES.md)
- [测试应用、镜像和验收边界](docs/rk3506/testing/README.md)
- [SPI 上板测试与引脚分配](docs/rk3506/testing/SPI.md)
- [历史记录说明](docs/rk3506/history/README.md)

直接修改这里的 `zephyr/`、`kernel-6.1/` 和 `device/`；不再运行旧的 install-to-sdk 或 sdk-sync。
Git 历史和远端配置已迁入本根目录，本轮驱动与 SDK 迁移分别提交。
供应商 SDK 基线和生成文件不自动纳入 Git，恢复工程仍需对应厂商 SDK，版本边界见 LAYOUT.md。
