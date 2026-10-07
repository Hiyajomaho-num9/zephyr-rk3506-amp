# SDK 根目录构建

应用、已有镜像和上板状态先看 [测试对照](testing/README.md)。最新复核见 [STATUS.md](STATUS.md)。

全部命令在 `OK3506_Linux_Source/` 执行。Python 需要至少 3.12、CMake 至少满足当前 Zephyr
要求；编译机已有 `~/venv-zephyr-py312` 和 `~/.local/bin/cmake`。交叉编译器使用本 SDK 的 prebuilts。

```sh
export PATH="$HOME/venv-zephyr-py312/bin:$HOME/.local/bin:$PATH"
export Python3_EXECUTABLE="$HOME/venv-zephyr-py312/bin/python"
./scripts/check_rk3506_zephyr_layout.sh
```

当前 defconfig 为 `OK3506-S-MINI_amp_nand_zephyr_defconfig`，支持 NAND 板的 Linux + Zephyr AMP。
CMSIS 和 picolibc 由 `zephyr/external_modules/` 显式提供，不需要另一个 west 工作区。

## 默认完整镜像

```sh
./build.sh amp
./build.sh kernel
./build.sh updateimg
```

默认 AMP 应用为 `zephyr/samples/subsys/ipc/rpmsg/rk3506_pingpong`。
每次 amp 命令从源代码重新构建选定应用，禁用旧的私有 prebuilt 快路径；
驱动改动和应用切换不会静默复用另一份固件。无需运行任何源码同步/安装脚本。

| 产物 | 位置 |
|---|---|
| Zephyr CMake 目录 | `output/zephyr/<应用名>/` |
| 最近一次 Zephyr 编译日志 | `output/zephyr-build.log` |
| 裸固件 | `output/zephyr.bin` |
| AMP / Linux boot 分区 | `output/firmware/amp.img`、`output/firmware/boot.img` |
| 完整升级包 | `output/firmware/update.img`，实际位于 `output/update/Image/update.img` |
| 已归档交付包 | `output/releases/` |

`updateimg` 打包现有分区文件，不隐式重编 rootfs、U-Boot。当前带 AMP 的 U-Boot 已验证，
如要重编 U-Boot，仍必须使用 `rk-amp.config`。完整包含 userdata，和只烧 amp 分区不是同一种更新范围。

## SPI 和 GPIO 测试镜像

SPI 内部回环：

```sh
RK_AMP_ZEPHYR_APP=zephyr/samples/rk3506/spi_loopback ./build.sh amp
```

SPI1 外部回环：

```sh
RK_AMP_ZEPHYR_APP=zephyr/samples/rk3506/spi_loopback \
RK_AMP_ZEPHYR_EXTRA_DTC_OVERLAY_FILE=external-spi1.overlay \
./build.sh amp
```

GPIO API 测试：

```sh
RK_AMP_ZEPHYR_APP=zephyr/tests/drivers/gpio/gpio_basic_api ./build.sh amp
```

该 GPIO 测试使用 GPIO3_A6/A7，SoM 129/128 脚，第 3 组中断；不要使用旧 gpio2 NAND 数据脚。

GPIO shell：

```sh
RK_AMP_ZEPHYR_EXTRA_CONF_FILE="$PWD/zephyr/boards/forlinx/ok3506b_s12/debug/gpio-shell.conf" \
RK_AMP_ZEPHYR_EXTRA_DTC_OVERLAY_FILE="$PWD/zephyr/boards/forlinx/ok3506b_s12/debug/enable_gpio3.overlay" \
./build.sh amp
```

统一使用 SDK 的 EXTRA_CONF_FILE / EXTRA_DTC_OVERLAY_FILE 转发；原样例私有的
`RK3506_DEBUG_DTS_OVERLAY` 已移除。环境变量只作用于当前命令。
不带这些变量重新运行 `./build.sh amp` 即恢复默认 pingpong。
切换应用会覆盖当前 amp 分区产物；随后运行 `updateimg` 就会打包这个应用。
如要交付默认完整包，应先不带测试变量执行 `./build.sh amp`。

## Linux 测试工具

```sh
./build.sh rk3506-tools
```

源码在 `tools/rk3506_rpmsg_char_ping/`，五个 ARM Linux 程序输出到
`output/tools/rk3506_rpmsg_char_ping/`。此命令只编译，不自动烧板或更新既有 rootfs。
底层 Makefile 也支持 `O=<输出目录>`，不再把新二进制放在源码旁。

## 验证

构建失败应返回非零，SDK 主入口吞掉 hook 错误码的问题已修复。仍应检查实际产物与配置，
交付前用厂商 rkImageMaker / afptool 回拆 update.img，逐项比较 SHA-256 和分区大小。
源代码构建通过不等于上板通过；Linux GIC 初始化清 AMP IRQ 使能的已知问题仍需处理。
