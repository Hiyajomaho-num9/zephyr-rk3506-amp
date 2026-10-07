# RK3506 BSP 结构检查清单

复核：2026-10-06；五目标证据为 9/28，SPI 内部回环完整包于 10/06 再次构建验证。
此清单和 `scripts/check_rk3506_zephyr_layout.sh` 随 SDK 根目录工程维护。
官方 Zephyr 文档以本源码树 `zephyr/doc/` 为准，芯片事实以 Rockchip 原厂资料为准。

- [x] SDK 是唯一开发根，Zephyr 是真实的 `zephyr/` 子目录，Git 工作树位于 SDK 根。
- [x] board / SoC / drivers / bindings / samples / tests 均在 Zephyr 原生目录。
- [x] GPIO、SPI、CAN、CRU、pinctrl 使用现有 YAML binding；真实构建校验通过。
- [x] 当前是完整 Zephyr 源码树，不要求外置模块的 `zephyr/module.yml`。
- [x] 样例的 BOARD/SOC/DTS_ROOT 仅保留 SDK 直接构建兼容胶水，不再将源码根自注册为模块。
- [x] CMSIS/picolibc 显式取本树 external_modules，不依赖其他 west 工作区。
- [x] 配置路径仅在 amp-zephyr.cfg 定义，ITS 保留 CPU/内存/镜像描述。
- [x] 五个 AMP 配置、五个 Linux 工具、内核与完整升级包已从 SDK 根验证构建。
- [x] 构建失败非零退出；整包回拆逐项哈希及分区大小检查通过。
- [ ] Twister 完整运行：当前 Python 环境仍缺依赖，未用构建通过冒充 Twister 通过。
- [ ] Linux GIC 对 AMP IRQ 的使能保护和对应 GPIO/CAN 中断硬件复验。
- [ ] GPIO 默认保留范围下沉到板级 DTS；当前依靠各测试 overlay 限制。
- [ ] 后续审查 mailbox 是否转为标准 MBOX 驱动，以及生产样例的诊断开关边界。

详细构建证据和已知限制见 STATUS.md；本轮未宣称新固件已上板。
10/06 重编 SPI AMP/内核并打包，回拆 9 项、分区大小、固件和 DTB 一致性检查通过。
Twister 依赖缺口仍在；本轮没有新增板上测试。
