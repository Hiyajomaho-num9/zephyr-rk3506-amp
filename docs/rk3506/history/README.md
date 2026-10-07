# 历史资料

本目录保留旧 SDK 记录、外置仓库交接和旧构建指南的原文，供追溯使用。
其中的旧路径、安装/同步命令、GPIO 引脚选择以及“待编译/待烧写”状态可能已经失效。
当前开发以根 README、`docs/rk3506/BUILD.md`、STATUS.md 和 testing/SPI.md 为准。

原始迁移前源码、Git 备份、缓存和文件映射保存在
`archive/layout-before-20260928-023401Z/`，不作为第二套开发工程。

## 按问题追溯

| 内容 | 入口 |
|---|---|
| Claude 9 月开发及 Codex 接手核验 | [external-repo-ZCODE-SYNC.md](external-repo-ZCODE-SYNC.md)，§14–§16 |
| 外置仓库原构建方式 | [external-repo-SDK-PACK.md](external-repo-SDK-PACK.md)，仅历史参考 |
| 迁移前 SPI 测试与引脚审核 | [SPI-TEST-before-sdk-root.md](SPI-TEST-before-sdk-root.md)；当前版见 [testing/SPI.md](../testing/SPI.md) |
| 早期 RPMsg、cache、CAN 和 BSP 审核报告 | [Zephyr 树内历史报告](../../../zephyr/doc/rk3506/README.md) |

当前整体进度只维护在 [STATUS.md](../STATUS.md)，这里保留当时的原文与判断。
