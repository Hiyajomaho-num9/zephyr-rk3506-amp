# RK3506 项目结构

2026-09-28 起，唯一开发根为本 SDK 的 `OK3506_Linux_Source/`。
Zephyr 是其中的实际目录，Git 工作树也以 SDK 为根；没有指向其他源码仓库的构建链接。

```text
OK3506_Linux_Source/
├── .git/                         原工程完整 Git 历史、索引和远端
├── build.sh                      SDK 原生构建入口
├── zephyr/                       完整 Zephyr 源码树
│   ├── boards/forlinx/ok3506b_s12/
│   │   └── debug/                GPIO shell 配置和安全引脚 overlay
│   ├── soc/rockchip/rk3506/
│   ├── drivers/                  GPIO、SPI、CAN、CRU、pinctrl 等
│   ├── samples/rk3506/            SPI、GPIO power-cycle 应用
│   ├── samples/subsys/ipc/rpmsg/rk3506_pingpong/
│   ├── tests/                    GPIO/CAN API、AMP/cache 验证
│   └── external_modules/         本树内的 CMSIS、picolibc
├── kernel-6.1/                   实际 Linux 内核和 AMP DTS
├── device/rockchip/               实际板级配置、ITS、SDK 构建 hook
├── tools/rk3506_rpmsg_char_ping/  Linux RPMsg/CAN 工具源码
├── scripts/                      结构检查等开发辅助工具
├── docs/rk3506/                  当前指南、测试和历史
├── output/                       全部新构建产物
│   ├── zephyr/<应用名>/          CMake 构建目录
│   ├── firmware/                 当前分区镜像和整包入口
│   ├── update/Image/update.img   SDK 整包实际文件
│   ├── tools/                    Linux 工具二进制
│   ├── validation/               验证日志与临时测试镜像
│   └── releases/                 保存的交付镜像
└── archive/                      旧布局备份，不参与构建
```

`buildroot/`、`u-boot/`、`rtos/`、`prebuilts/` 等供应商目录保留原 SDK 布局。
SDK 自带的 `kernel -> kernel-6.1`、`rockdev -> output/firmware` 等内部链接继续使用；
Zephyr 源码不再通过链接跨工程引用。

## 每类内容只有一个修改位置

| 内容 | 修改位置 |
|---|---|
| Zephyr BSP、驱动、应用和测试 | `zephyr/` 对应原生子目录 |
| Linux AMP 内存、IRQ、时钟、GPIO 分配 | `kernel-6.1/arch/arm/boot/dts/` |
| Zephyr 应用/板型/模块/构建目录 | `device/rockchip/.chips/ok3506/amp-zephyr.cfg` |
| AMP FIT 的 CPU、加载地址和内存布局 | 同目录 `amp_linux_zephyr.its` |
| Zephyr 编译与 FIT 打包实现 | `device/rockchip/common/scripts/mk-amp.sh` |
| Linux 测试工具 | `tools/rk3506_rpmsg_char_ping/` |
| 当前说明和进度 | `docs/rk3506/`，从根 README 进入 |

ITS 不再重复写 Zephyr 源码路径。旧 `linux/` overlay 和 `sdk/` 镜像目录均已退役；
不再维护“仓库副本 / SDK 副本”两份代码，也没有双向同步步骤。

## Git 边界

- `.git` 由原项目实际移动，原 HEAD `cbb9882a6273687b09d1cb462b43722a3e11659e`、分支、
  origin/upstream、索引和未提交内容均保留；没有重建空仓库或重写提交历史。
- `zephyr/` 的历史路径不变。原 `linux/...` 文件现在位于 SDK 原生路径，调试 overlay
  位于板级目录，文档位于 docs；这些移动随 2026-10-07 的 SDK 单根迁移提交记录。
- 继续跟踪 Zephyr 以及 RK3506 的 SDK 配置、DTS、工具和构建入口；供应商 SDK 基线、
  下载包、二进制、output、archive 不批量加入 Git。新增修改其他供应商文件时，
  明确添加该源文件（必要时 `git add -f <文件>`），不要把整个 SDK 生成目录加入版本库。
- 在其他机器恢复本项目，需要对应供应商 SDK 基线，再覆盖本仓库跟踪的文件。

## 旧路径和回退材料

原 `~/my_code/zephyr-rk3506-amp/` 和 `~/rk3506/amp-images/` 只留下迁移说明，
没有源码、Git 工作树或构建软链接。原镜像现位于 `output/releases/`。

本次备份：`archive/layout-before-20260928-023401Z/`，包含原 `.git` 的副本、
未提交文件包、工作树/索引 patch、迁移映射、旧 SDK Zephyr 模块、旧 overlay/同步脚本和缓存。
不要在这些备份中继续开发。`docs/rk3506/history/` 保留历史原文，其旧路径与旧接线说明
不代表当前配置。供应商原厂资料和开发板手册仍保留在 SDK 交付包原位置。
