# ZCode 会话同步文档 — zephyr-rk3506-amp 工作状态

> 本文档由桌面机侧 ZCode 会话（sess_53d9e699，Windows 10.0.0.5）维护。
> **任何在编译机上启动的 ZCode 会话（如 sess_73de19c5）开工前先读本文件**，
> 这是两台机器之间唯一可靠的同步通道（ZCode 会话库按主机隔离存储，跨主机不可读）。

最后更新：2026-09-28（UTC；编译机当地 2026-09-27，Codex 完成完整 update.img，见 §16）。
Linux 时钟/PWM DTS 修复已同步到 SDK 并编入新 boot.img；当前 RPMsg pingpong 已重编打包。
整包已回拆校验，尚未上板；Linux GIC 的 AMP IRQ 使能保护仍待修复。
Claude 于 2026-09-25 完成的 SPI 测试镜像与 GPIO3_A6/A7 引脚方案仍有效，测试步骤见 `SPI-TEST.md`。

## 1. 仓库与当前状态

- GitHub: https://github.com/Hiyajomaho-num9/zephyr-rk3506-amp
- 本地工作副本: `~/my_code/zephyr-rk3506-amp`（官方 Zephyr 以 subtree 形式在 `zephyr/` 子目录）
- 本地 `main` = `cbb9882a627`，相对本地记录的 `origin/main`（`b3932a931cb`）领先 4 个提交、落后 0 个。
  本次未 fetch，远端引用以本地记录为准。工作树另有 28 个已跟踪文件修改、21 个未跟踪文件
  （`git status --porcelain=v1 -uall`，含本文档；不是只有提交链中的内容）。
- 提交链（main）:
  ```
  cbb9882a627  samples: add rk3506 gpio_power_cycle stress-signal app  ← 本地 HEAD
  9385a176559  samples: pingpong: debug hooks for GPIO shell bring-up
  a5221d5c7f5  docs: add SDK pack guide (amp.img/update.img from this tree)
  3317454b430  kconfig: fix CLOCK_CONTROL dependency loop; enable CRU at SoC level
  b3932a931cb  drivers: gpio: add Rockchip RK3506 GPIO driver
  9be650dd290  Merge Zephyr upstream main (2f0bc11264a) into zephyr/ subtree
  31d137389e2    └─ Squashed upstream v4.4.0 → 2026-09-01 main
  e6092401aad  Apply RK3506 AMP customization on top of Zephyr v4.4.0
  ```
- **SDK 打包线已切换到本仓库**（见 §9 与 SDK-PACK.md），`output/firmware/amp.img`
  现在直接由本仓库固件生成

## 2. 上游合并要点（已完成的认知）

- 上游 `zephyrproject-rtos/zephyr` main 合入 `zephyr/` 子树，无 forlinx/rk3506 内容冲突
- AMP 定制层 3815 文件（A=3789/M=11/D=15）：can_rk3506、forlinx 板级、RK3506 SoC/时钟/pinctrl/复位、RPMsg
- M 文件：`arm_mmu.c`（RK3506 上通用 MMU init 不开 DCACHE，交给 soc.c 决定；2026-09-23 前
  这里误写成不存在的 `CONFIG_RK3506_PRIVATE_ICACHE`，见 §14）、`soc.yml`、各驱动单行注册
- D 文件（故意删除，勿恢复）：`drivers/i2c/target/*`（eeprom 部分）、`tests/boards/neorv32`、`scripts/tests/build/*`
- 合并冲突处理原则：**AMP 定制全保留，上游新功能全纳入**

## 3. GPIO 驱动（b3932a931cb，已推送）

- 驱动: `zephyr/drivers/gpio/gpio_rk3506.c`（358 行，TRM Part 1 §17）
- 5 个 32-pin bank（gpio0-4），寄存器 L/H 半字 + **高位 per-bit 写使能**（bit n+16 门控 bit n）
- 中断：电平/边沿/**双沿走独立 GPIO_INT_BOTHEDGE 寄存器**、EOI 应答
- 时钟：`clock_control_on` 走 CRU；**gpio0 pclk 门控在 PMU CRU (0xFF9B0000) GATE_CON00 bit8**；
  gpio1=GATE_CON03(0x80c)bit8、gpio2=GATE_CON12(0x830)bit14、gpio3/4=GATE_CON13(0x834)bit0/2
- dts: gpio0-4 默认 disabled，每 bank 4 个 GIC SPI（SPI 0-19，硬件号-32）
- binding: `dts/bindings/gpio/rockchip,rk3506-gpio.yaml`

## 4. 构建环境（重要）

- **python 必须 ≥3.12**：用 `~/venv-zephyr-py312`（持久，勿用 /tmp/zpy312——重启会丢）
- cmake 3.28.4: `~/.local/bin/cmake`（PATH 需含 `~/.local/bin`）
- 工具链: `/usr/bin/arm-none-eabi-`（gcc 10.3.1）
- 上游 main 需要装 **west**（否则 Kconfig.modules 为空 → CMSIS 双定义 Kconfig 崩溃）
- 构建命令模板：
  ```sh
  export PATH=$HOME/venv-zephyr-py312/bin:$HOME/.local/bin:$PATH
  cd ~/my_code/zephyr-rk3506-amp/zephyr
  export ZEPHYR_BASE=$(pwd)
  cmake -GNinja -B /tmp/build -S samples/subsys/ipc/rpmsg/rk3506_pingpong \
    -DBOARD=ok3506b_s12_amp_uart1 \
    -DZEPHYR_TOOLCHAIN_VARIANT=cross-compile -DCROSS_COMPILE=/usr/bin/arm-none-eabi- \
    -DPython3_EXECUTABLE=$HOME/venv-zephyr-py312/bin/python \
    -DEXTRA_ZEPHYR_MODULES="$PWD/external_modules/hal/cmsis;$PWD/external_modules/lib/picolibc"
  cmake --build /tmp/build
  ```

## 5. 已知集成坑（勿踩）

1. 新上游 `gpio.h` 不再自动包含 `gpio_utils.h`，`GPIO_PORT_PIN_MASK_FROM_DT_INST` 需
   显式 `#include <zephyr/drivers/gpio/gpio_utils.h>`
2. **GPIO_RK3506 不得 `select CLOCK_CONTROL`**（3317454b430）：会与上游
   `I2S_SILABS_SIWX91X`（depends CLOCK_CONTROL + select GPIO）构成结构性依赖环，
   全树任何构建都炸。改为 SoC defconfig 层 `default y`（CLOCK_CONTROL +
   CLOCK_CONTROL_RK3506_CRU），驱动侧 `depends on`
3. DTS overlay 引用 gpio 前需 `&gpioX { status = "okay"; }` 且 overlay 头部
   `#include <zephyr/dt-bindings/gpio/gpio.h>`（gpio_basic_api overlay 实测踩过）
4. 宏内嵌 `COND_CODE_1` 包含逗号会炸，构造静态结构体用独立定义 + `COND_CODE_1` 只选指针

## 6. 未完成事项（下一步）

- [ ] 整理未提交改动；本地相对 `origin/main` 领先的 4 个提交尚待推送（见 §1）。
- [x] 将 §14 第 6、7 条的 Linux 时钟持有 / GPIO0_A2 PWM 修复同步到 SDK，并重建、校验 boot.img
      （2026-09-28，见 §16；硬件验收仍待上板）。
- [ ] 修复 §14 第 5 条的 Linux GIC 初始化覆盖 AMP 中断使能问题，再验证依赖中断的外设。
- [ ] GPIO 硬件实测（需接跳线）：GPIO3_A6↔GPIO3_A7（SoM 129↔128 脚）短接跑 `tests/drivers/gpio/gpio_basic_api`。
      原计划的 gpio2 pin4/5 是 SPI NAND 的 FSPI_D2/D3，不能用，见 §14 第 8 条。
      测试构建命令（板级 overlay 自动带上）：
      ```sh
      export PATH=$HOME/venv-zephyr-py312/bin:$HOME/.local/bin:$PATH
      cd ~/my_code/zephyr-rk3506-amp/zephyr && export ZEPHYR_BASE=$(pwd)
      cmake -GNinja -B /tmp/build-gpio -S tests/drivers/gpio/gpio_basic_api \
        -DBOARD=ok3506b_s12_amp_uart1 \
        -DZEPHYR_TOOLCHAIN_VARIANT=cross-compile -DCROSS_COMPILE=/usr/bin/arm-none-eabi- \
        -DPython3_EXECUTABLE=$HOME/venv-zephyr-py312/bin/python \
        -DEXTRA_ZEPHYR_MODULES="$PWD/external_modules/hal/cmsis;$PWD/external_modules/lib/picolibc"
      cmake --build /tmp/build-gpio   # 2026-09-25 改成 GPIO3_A6/A7 后重编通过，等待上板
      ```
- [ ] 待实测项：双沿触发、EOI 清边沿中断、gpio0 PMU 域门控
- [ ] 后续外设适配候选：PWM / I2C（TRM §31 / §32）。SPI（§29）已完成，见 §14 第 4 条与 `SPI-TEST.md`
- [ ] SPI 上板（2026-09-25 出镜像）：先跑 `SPI-TEST.md` 第 3、4 节；第 5 节（fix A）要新编 boot.img

## 7. 其他资源位置

- TRM 全文缓存: `/tmp/rk3506_trm.txt`（7 万行，重启会丢；原件
  `~/rk3506/OK3506B-S12_Linux6.1.99/原厂资料/Rockchip RK3506 TRM Part 1 V1.0-20240902.pdf`）
- AMP 布局基线: `~/rk3506/phase-archive/phase5.1-zephyr-amp-layout-20260515/rk3506_amp_layout.h`
  （0x03b00000 shmem / 0x03c00000 rpmsg vring / 0x03d00000 rpmsg-dma）
- 相关会话参考: 桌面机会话 sess_53d9e699（本文件即其交接产物）

## 8. 编译机会话工作记录（sess_73de19c5，2026-09-03）

- 开工核对：仓库链顶 b3932a931cb、venv/cmake/工具链/west 全部就绪，TRM 缓存仍在
- **发现并修复全树构建阻塞**（3317454b430）：GPIO_RK3506 `select CLOCK_CONTROL` 与上游
  I2S_SILABS_SIWX91X 构成 Kconfig 依赖环（详见 §5.2）。pingpong 样例此前能过纯属侥幸
  （CAN_RK3506 也 select 了 CLOCK_CONTROL，但环仍在，kconfig 解析直接失败）
- gpio_basic_api：新增板级 loopback overlay（gpio2 pin4↔pin5），编译通过；已提交待推送
  【2026-09-25】这对脚是 SPI NAND 的数据线，overlay 已改为 GPIO3_A6/A7（未提交），见 §14 第 8 条
- 遗留：本提交尚未 push（桌面机侧可执行 `git push`，或告知编译机直接推）

## 9. SDK 打包线切换（2026-09-03，编译机完成）

打包线固件源已从 SDK 内旧工程切到本仓库，**amp.img 现在由本仓库生成**：

- 机制与用法详见 **SDK-PACK.md**（本仓库根目录）
- SDK 侧改动（3 处，桌面机同步时注意）：
  1. `device/rockchip/.chips/ok3506/amp_linux_zephyr.its`：compile 块全部
     指向 `zephyr-rk3506-amp-link/...`（SDK 内 symlink → `~/my_code/zephyr-rk3506-amp`）
  2. `device/rockchip/.chips/ok3506/amp-zephyr.cfg`：重写，全部变量可被环境
     覆盖（`RK_AMP_ZEPHYR_TREE` 一把钥匙）；默认带 cmsis+picolibc EXTRA_MODULES
  3. `device/rockchip/common/scripts/mk-amp.sh`：+3 行，支持
     `RK_AMP_ZEPHYR_EXTRA_MODULES`（不传会静默退 minimal libc，见 SDK-PACK.md）
- symlink：`$SDK/zephyr-rk3506-amp-link → ~/my_code/zephyr-rk3506-amp`
- 旧 `zephyr-rk3506/` 与 `~/zephyr-work/zephyr`（已不存在）废弃，勿再引用
- 验证：全量重建/快路径/源码变更自动重建三态全通过，
  amp.img（95232B）内嵌 zephyr.bin sha256 `d1f50765…` 与本仓库 `out/zephyr.bin` 一致
- 打包命令：
  ```sh
  cd ~/rk3506/OK3506B-S12_Linux6.1.99/SDK/OK3506_Linux_Source
  export PATH=$HOME/venv-zephyr-py312/bin:$HOME/.local/bin:$PATH
  Python3_EXECUTABLE=$HOME/venv-zephyr-py312/bin/python ./build.sh amp
  ```
- 未做：`build.sh updateimg` 整包未重跑（rootfs/kernel 未变，amp 分区镜像单独
  烧写或后续整包时再出）；桌面机侧需同步这 3 处 SDK 改动

## 10. GPIO 调试版固件（2026-09-03，编译机完成）

**已产出带 GPIO 驱动 + shell 的 amp.img**（`output/firmware/amp.img`，103424B，
zephyr.bin sha256 `994ed098…`），RPMsg/缓存基线不变，仅叠加：

- `boards/debug/gpio-shell.conf`：CONFIG_GPIO / GPIO_RK3506 / GPIO_SHELL(+info/
  toggle/blink)
- `boards/debug/enable_gpio2.overlay`：仅启用 gpio2（pin4/5 = 计划环回脚）
  【2026-09-25 改】gpio2 没有空脚（A0～A5 是 SPI NAND，B0～C0 是 RMII0），现在 17 个脚全部
  reserved，只留作 bank 级排查。要在 shell 里动脚改用新的 `boards/debug/enable_gpio3.overlay`
  （只放出 GPIO3_A6/A7，`rockchip,irq-group = <3>`），见 §14 第 8 条
- 样例 CMakeLists 新增 `RK3506_DEBUG_DTS_OVERLAY` 环境钩子（DTC_OVERLAY_FILE
  必须在 find_package(Zephyr) 前设置，已处理）

重建调试版：
```sh
cd ~/rk3506/OK3506B-S12_Linux6.1.99/SDK/OK3506_Linux_Source
export PATH=$HOME/venv-zephyr-py312/bin:$HOME/.local/bin:$PATH
export Python3_EXECUTABLE=$HOME/venv-zephyr-py312/bin/python
export RK_AMP_ZEPHYR_EXTRA_CONF_FILE=$HOME/my_code/zephyr-rk3506-amp/boards/debug/gpio-shell.conf
export RK3506_DEBUG_DTS_OVERLAY=$HOME/my_code/zephyr-rk3506-amp/boards/debug/enable_gpio3.overlay
./build.sh clean-amp && ./build.sh amp
```
恢复生产版（不带 GPIO）：unset 两个变量后 `./build.sh clean-amp && ./build.sh amp`

调试用法：uart1 console（1500000 8N1；板级 DTS 从第一版起就是 1500000，这里原来写的 115200 有误）shell 里 `gpio info/get/set/toggle`；
`devmem` 直读 CRU 0x834 bit0（gpio3 门控）与 0xff1d0000（gpio3 寄存器）对照 TRM §17
（2026-09-25 前这里写的是 gpio2：0x830 bit14 与 0xff1c0000；9/3 那版调试固件用的是 gpio2 overlay）。
mk-amp.sh 又补了一处：支持 `RK_AMP_ZEPHYR_EXTRA_CONF_FILE`（EXTRA_CONF_FILE 叠加
语义；原 CONF_FILE 会整个替换 prj.conf 导致 CONFIG_SHELL 丢失，勿用）。

提交链追加：`9385a176559 samples: pingpong: debug hooks for GPIO shell bring-up`

## 11. 开关机压测固件（2026-09-04，编译机完成）

**压测版 amp.img 已产出**：`output/firmware/amp.img`（54272B，zephyr.bin sha256
`e97439a0…`），应用 `zephyr/samples/rk3506/gpio_power_cycle`（cbb9882a627）。

- 信号：**GPIO0_A2**（gpio0 pin2；IOMUX 复位默认 GPIO 功能）。
  【2026-09-25 更正】原来写"Linux 未占用"是错的：SDK 现有 DTB 里 `pwm@ff931000` 的 default
  pinctrl 会在 Linux 启动时把 A2 切成 PWM0_CH1，见 §14 第 7 条
- 时序（CPU2 上电起算，console uart1 每步 LOG_INF 带秒数）：
  开机立即拉高 → 50s 拉低 2s → 拉高 15s → 拉低 2s → 常高
  （这是 9/4 版。2026-09-24 版改为拉高 50 s → 拉低 250 ms → 拉高 20 s → 拉低 250 ms → 常高，
  按绝对时间调度，见 `samples/rk3506/gpio_power_cycle/sample.yaml`）
- 重新打包（app 名经 env 注入，ITS zephyr_app 字段已置空回落 cfg）：
  ```sh
  cd ~/rk3506/OK3506B-S12_Linux6.1.99/SDK/OK3506_Linux_Source
  export PATH=$HOME/venv-zephyr-py312/bin:$HOME/.local/bin:$PATH
  export Python3_EXECUTABLE=$HOME/venv-zephyr-py312/bin/python
  export RK_AMP_ZEPHYR_APP=$HOME/my_code/zephyr-rk3506-amp/zephyr/samples/rk3506/gpio_power_cycle
  ./build.sh clean-amp && ./build.sh amp
  ```
- 恢复 pingpong 生产包：unset RK_AMP_ZEPHYR_APP（或改指 pingpong 路径）重跑
- **坑（勿踩）**：比 `samples/subsys/ipc/rpmsg/*` 浅的应用目录，CMakeLists 不得把
  仓库根 append 进 EXTRA_ZEPHYR_MODULES——west 扫描器会因根下 zephyr/{CMakeLists.txt,
  Kconfig} 把仓库根当模块，递归 source 主 Kconfig 直接炸。BOARD/SOC/DTS_ROOT 胶水足够
- 压测注意事项：上电瞬间 CPU2 固件引导前 GPIO0_A2 为 IOMUX 复位态（输入浮空/高阻），
  外部电源控制器若把浮空当有效电平需外接下拉/上拉；固件引导后（秒级）才进入受控时序

## 12. 两版镜像归档与 ping 超时澄清（2026-09-08）

镜像归档在编译机 `~/rk3506/amp-images/`：

| 文件 | 应用 | zephyr.bin sha256 前缀 |
|---|---|---|
| `amp-powercycle-gpio0a2.img` | gpio_power_cycle（压测） | `e97439a0` |
| `amp-pingpong-production.img` | rk3506_pingpong（RPMsg 生产版） | `dbe9dea4` |

**ping 超时澄清**：`rk3506_rpmsg_char_ping` 的通道名/dst 是硬编码
（`rpmsg-ap3-ch0`/0x3003），端点由 Linux 本地创建；Linux 的 rpmsg 总线又是 dts
静态声明（rockchip,rpmsg @0x3c00000）。所以 `/dev/rpmsg_ctrl0` 存在 ≠ CPU2 活着。
烧压测固件（无 RPMsg）时 ping 超时是**预期行为**，与 GPIO 驱动无关。

判定 CPU2 固件类型的两个硬指标：
1. `ls /sys/bus/rpmsg/devices` 出现 `virtio0.rpmsg-ap3-ch0.-1.12291` = 有 NS announce
   = 跑的是 pingpong（或任何 RPMsg 应用）
2. Linux 直读 GPIO0_A2（压测固件才有效）：`devmem 0xff940008` bit2=1（方向输出）、
   `devmem 0xff940000` bit2 电平随 50s/2s/15s 时序变化（9/24 版是 50 s/250 ms/20 s/250 ms）。
   【2026-09-25 注】DR/DDR 只反映 Zephyr 写了什么，不代表引脚电平。A2 的 IOMUX 被 Linux
   切走时引脚不跟随，要先看 `devmem 0xff950000` [11:8] 是不是 0，见 §14 第 7 条

**切应用必须 `./build.sh clean-amp`**：prebuilt 快路径只比较 app/boards/soc/dts
的新旧，切换 RK_AMP_ZEPHYR_APP 时旧缓存反而更新，会静默打出错误固件。

## 13. ping 超时根因：板上 U-Boot 不带 AMP（2026-09-08 修复，已烧写验证）

**根因链**（全部实锤）：
1. amp 分区内容正确（FIT 魔数/sha256/字符串三重验证 = 新 pingpong 固件）
2. U-Boot 上电日志无任何 `AMP:` 行 → CPU2 从未被释放
3. 板上 U-Boot = `Jun 09 2026 06:53:43` 构建 = SDK `u-boot/uboot.img`（06:53:45）
4. 该构建 `.config` 里 **`# CONFIG_AMP is not set`**：6 月 9 日 06:53 重跑 defconfig
   时把 03:31 那次带 AMP 的配置冲掉了（`rk-amp.config` fragment 未合入）
5. `build_uboot()`（70-loader.sh）编译段整体被注释，`build.sh uboot` 只做符号链接，
   不会重新生成带 fragment 的配置

**修复（2026-09-08 已完成编译）**：
```sh
cd ~/rk3506/OK3506B-S12_Linux6.1.99/SDK/OK3506_Linux_Source/u-boot
grep -v "# CONFIG_AMP is not set" .config | grep -v "# CONFIG_ROCKCHIP_AMP is not set" > .config.new
cat configs/rk-amp.config >> .config.new && mv .config.new .config
make ARCH=arm CROSS_COMPILE=$SDK/prebuilts/gcc/linux-x86/arm/gcc-arm-none-eabi-10-2020-q4-major-x86_64-linux/bin/arm-none-eabi- olddefconfig
export KCFLAGS="-Wno-error"   # rbsb.c 等被新拉入的文件有无害告警
./make.sh                      # 不带参数 = 用现有 .config，不重跑 defconfig
```
产物：`~/rk3506/amp-images/uboot-with-amp-20260908.img`（sha256 `326ee08b…`，
CONFIG_AMP=y 已在 auto.conf 确认，"Brought up cpu" 字符串在镜像内）。
旧 U-Boot 备份：`uboot-noamp-backup-20260609.img`。

**已完成**（用户 2026-09-23 确认：Codex 时期已烧写并验证通过，当时未回写本文档）。
编译机侧核对：SDK `u-boot/uboot.img`（2026-09-07 19:01）与
`~/rk3506/amp-images/uboot-with-amp-20260908.img` 字节一致（sha256 `326ee08b…`），
`u-boot/.config` 含 `CONFIG_AMP=y` / `CONFIG_ROCKCHIP_AMP=y`；`rockdev/uboot.img` 与
`output/firmware/uboot.img` 都是它的符号链接，后续 `build.sh updateimg` 整包会自然带上。
烧写方式备忘：RKDevTool，spi-nand 存储地址 `0x400000`（mtd1 uboot 分区），只烧这一个分区。
（`0x400000` 是字节偏移。RKDevTool 地址栏按扇区（512 B）算：uboot `0x00002000`、boot `0x00004800`、
amp `0x00059800`，出自 `device/rockchip/.chips/ok3506/parameter-mini-amp-nand.txt`。）
正常上电日志应有：
```
AMP:        desc: zephyr-core2
AMP:         cpu: 0xf02
AMP: Brought up cpu[f02] with state 0x..., entry 0x03e00000 ...
OK
```
→ Linux 起来后 `/sys/bus/rpmsg/devices` 应出现 `virtio0.rpmsg-ap3-ch0.-1.12291`
→ `rk3506_rpmsg_char_ping` 应通。

**教训**：任何 U-Boot 重编都必须合入 `configs/rk-amp.config`（或用带 fragment 的
打包流程）；`build.sh uboot` 当前不编译 U-Boot，不要依赖它。

## 14. 接手核对与待修清单（2026-09-23，Claude Code 编译机会话）

**工作树状态**（§1 的提交链已过期）：本地 main = `cbb9882a627`，比 origin/main
（`b3932a931cb`）领先 4 个提交（3317454b430 / a5221d5c7f5 / 9385a176559 / cbb9882a627），
均未推送。另有 20 个文件已改未提交（约 +1011/-398 行：CRU 时钟、pinctrl/RMIO 重写、
gpio 驱动、rk3506.dtsi、gpio_power_cycle 样例等）；未跟踪文件包括整套 SPI 驱动
（`drivers/spi/spi_rk3506.c` + `Kconfig.rk3506` + binding + `samples/rk3506/spi_loopback/`）
和本文档 `ZCODE-SYNC.md` 自身。

**代码缺陷（按优先级）与处理**：

1. 【已修 2026-09-23】`zephyr/arch/arm/core/mmu/arm_mmu.c` 守卫原来写的是
   `CONFIG_RK3506_PRIVATE_ICACHE`，Kconfig 实际符号是 `CONFIG_SOC_RK3506_PRIVATE_ICACHE`，
   守卫永远不成立：通用 MMU 初始化无条件打开 D-cache，随后 `soc.c` 在两个 cache 选项
   都关闭时直接清 SCTLR.C 且不 clean，中间窗口的脏行被丢弃。
   修法：守卫改为 `!defined(CONFIG_SOC_RK3506)`，RK3506 上 D-cache 只由
   `rk3506_apply_cache_policy()` 决定（PRIVATE_DCACHE=y 时 invalidate 后打开）；该函数末尾
   新增"C 从 1 变 0 前先 `L1C_CleanInvalidateDCacheAll()`"的兜底。pingpong（两项均 y）
   最终状态不变，只是 D-cache 从 soc_early_init_hook 起生效而不是从 MMU init 起；
   `gpio_power_cycle` / `spi_loopback`（prj.conf 未开 cache 选项）不再有脏行丢失窗口。
2. 【已修 2026-09-23】GIC 分发器完整初始化：`soc/rockchip/rk3506/Kconfig.defconfig` 新增
   `GIC_SAFE_CONFIG default y`（上游选项，i.MX8M 同款）。效果：GICD_CTLR 已使能时跳过
   分发器初始化；自己初始化时 SPI 目标全清零；`arm_gic_irq_enable()` 对每个使能的 SPI
   按位或本核掩码，不再覆盖 Linux 的目标位。原因：CPU2 起来时上游 `gic_dist_init()`
   会清 GICD_CTLR、重写全部 SPI 目标/优先级/分组、关掉全部 SPI 使能，此前不出事只因
   U-Boot 先放 CPU2、Linux 后初始化盖回去，Linux 运行中单独重启 CPU2 会打掉 Linux 全部外设中断。
   `SOC_RK3506_RPMSG_MBOX_GIC_ROUTE`（手写 GICD_ITARGETSR 整字节覆盖）改为
   `depends on !GIC_SAFE_CONFIG` 的 legacy 路径，默认不再编译；`rk3506_rpmsg_platform.c`
   的 target before/after 探针改为 GIC_V2 下总是采样，`compiled_flags` 新增
   BIT(6)=GIC_SAFE_CONFIG。**需上板复验** RPMsg ping（IRQ 176 仍应落到 CPU2：
   `rk3506_rpmsg_platform_gic_target_after()` 读到 bit2 置位）。
   Linux 侧依据（SDK `kernel-6.1/drivers/irqchip/irq-gic.c` `gic_dist_init()`，
   `CONFIG_ROCKCHIP_AMP`）：Linux 初始化时对 `amp-irqs` 里的中断**精确写入** GICD_ITARGETSR =
   amp cpumask（176 → 0x4）与优先级（0xd0），其余 SPI 写成启动核掩码。所以 Linux 起来后 176 的
   目标与 Zephyr 之前写了什么无关，"按位或"只影响 Linux 起来前那一小段窗口（那时没人发 mailbox）。
   `drivers/soc/rockchip/rockchip_amp.c` 的 `/sys/rk_amp/boot_cpu`（`echo on 0xf02`，走 SiP SMC
   重启 CPU2）只认 DT `amp-cpus` 子节点登记过的 CPU；本板 `rk3506-amp.dtsi` 与
   `OK3506-S-MINI_amp_nand.dts` 都没有 `amp-cpus`，现在执行只会打印 `cpu[f02] is unavailable`。
   所以"Linux 运行中重启 CPU2"目前不可达，GIC_SAFE_CONFIG 属于预防：将来加 `amp-cpus` 或做
   运行时重载时，Zephyr 见 GICD_CTLR 已使能就跳过分发器初始化。
3. GPIO 驱动：用户 2026-09-23 确认已上板跑过、可用（此前"从未上板"的判断撤回）。
   双沿 / EOI / gpio0 PMU 域门控的逐项确认仍可选做。
4. 【已处理 2026-09-25】SPI 驱动整合完成，保持纯轮询（无 IRQ_CONNECT，dtsi 节点无 interrupts，
   `amp-irqs` 无 SPI 条目）。不用中断，所以不受第 5 条影响。测试应用 `samples/rk3506/spi_loopback`，
   见本节末尾"SPI 驱动整合"一段，上板步骤见仓库根目录 `SPI-TEST.md`。
5. 【新发现，未处理】**Linux 启动会清掉 CPU2 所有 SPI 的使能位。** SDK
   `kernel-6.1/drivers/irqchip/irq-gic-common.c` `gic_dist_config()` 对 32..gic_irqs 全部 SPI 写
   `GICD_ICACTIVER` / `GICD_ICENABLER` = 0xffffffff，**没有** amp-irqs 例外（只有优先级循环有例外）。
   时序上 U-Boot 先放 CPU2、Linux 后跑 GIC 初始化，所以 Zephyr 在启动阶段 `irq_enable()` 过的每个
   SPI（mailbox 176、UART1 67、CAN0 77、GPIO 35/39/43/47/51）都会在 Linux 起来时被关掉一次。
   RPMsg 路径之所以活着，是因为 `rk3506_rpmsg_platform.c` 的 `platform_interrupt_enable/disable()`
   每次被 RPMsg-Lite 调用都重新置位 GICD_ISENABLER，再加 pingpong 的轮询兜底（prj.conf 故意不开
   `IRQ_ONLY_AFTER_NS`）；shell 串口用 polling 后端大概也是同一原因。任何依赖中断的 Zephyr 驱动
   （GPIO 中断、CAN、中断驱动 UART）如果只在设备初始化时 `irq_enable()` 一次，Linux 起来后就是死的。
   可选修法：(a) Linux 侧 `gic_dist_config()` 的两个清零循环跳过 `rockchip_amp_need_init_amp_irq()`
   为真的中断（改动在 SDK kernel，需要加进 `scripts/sdk-sync.sh` 清单）；(b) Zephyr 侧在 RPMsg
   link-up 后统一重新 `irq_enable()` 一遍自己的 SPI。(a) 更根本。
6. 【新发现 2026-09-25，仓库已改，未上板】**Linux 起来几秒后会关掉 SPI0、SPI1、CAN0 的时钟。**
   SDK `kernel-6.1/drivers/clk/clk.c` 的 `clk_disable_unused()` 挂在 late_initcall_sync，dmesg 打印
   `clk: Disabling unused clocks`。它关掉所有 enable 计数为 0、又没有 `CLK_IGNORE_UNUSED` 的时钟。
   这 6 个时钟在 `drivers/clk/rockchip/clk-rk3506.c` 里 flags 都是 0，Linux 侧对应节点又都是 disabled，
   没人 enable 过，所以全被关掉：CRU GATE_CON12（`0xff9a0830`）bit10～13 = PCLK_SPI0 / CLK_SPI0 /
   PCLK_SPI1 / CLK_SPI1，GATE_CON13（`0xff9a0834`）bit4～5 = HCLK_CAN0 / CLK_CAN0（1 = 关）。
   SDK DTS 的 bootargs 不带 `clk_ignore_unused`（amp_nand 直接 include linux_nand.dts）。
   pclk 关着时访问控制器寄存器会把总线挂死。
   UART1 和 timer 没事，是因为 `&rockchip_amp` 的 `clocks` 列了它们（HCLK_M0、STCLK_M0、SCLK_UART1、
   PCLK_UART1、PCLK_TIMER、CLK_TIMER0_CH5）：rockchip_amp 的 probe 用 `devm_clk_bulk_get_all()` +
   `clk_bulk_prepare_enable()` 把它们 enable 了，计数不为 0，就不会被关。
   处理：
   - SPI 驱动兜底：每次传输前用 `clock_control_get_status()` 查 CRU，被关了就打一行 LOG_WRN 再重开。
     只是兜底。Linux 恰好在传输中间关时钟的话，这次传输可能失败，也可能挂死。
   - CAN 驱动没有兜底。pingpong 里 CAN 线程响应 RPMsg 的 `can:tx:` / `can:rx:` / `can:txrx:`，
     Linux 起来后再发这些请求，CPU2 会在 hclk 关着时访问 CAN0。fix A 上板之前，pingpong 的 CAN 功能不要当真。
   - fix A：仓库 `linux/kernel-6.1/arch/arm/boot/dts/OK3506-S-MINI_amp_nand.dts` 的 `&rockchip_amp`
     `clocks` 加上这 6 个时钟。**只在仓库里，SDK 未装，boot.img 未编。** 上板步骤见 `SPI-TEST.md` 第 5 节。
     probe 失败时 dmesg 有 `failed to prepare enable clks`，`/sys/rk_amp/` 不出现。CPU2 照常运行
     （CPU2 由 U-Boot 拉起，amp-irqs 路由由 GIC 驱动读 DT 完成，都不靠这个 probe），只是退回现状。
7. 【新发现 2026-09-25，仓库已改，未上板】**Linux 会把 GPIO0_A2 切成 PWM0_CH1。**
   板上 DTB（SDK 2026-06-03 编译）里 `pwm@ff931000`（标签 `pwm0_4ch_1`）是 okay，pinctrl 只有 `default`
   一个状态：`rm-io2-pwm0-ch1`（mux 0x2e）。内核 `CONFIG_PWM_ROCKCHIP=y`。
   驱动核心在 probe 之前先套 default 状态。`pinctrl-rockchip.c` 的 `rockchip_set_mux()` 先调
   `rockchip_set_rmio()` 把 RM_IO2（`0xff910088` [6:0]）写成 0x2e - 15 = 0x1f（PWM0_CH1），
   再把 A2 的 IOMUX（`0xff950000` [11:8]）写成 7（RM_IO）。
   pwm-rockchip 要的是 `active` 状态，找不到就 probe 失败（`No active pinctrl state`），失败后复用不还原。
   debugfs `pinmux-pins` 里 pin 2 可能显示 UNCLAIMED，不能用它判断，要看寄存器。
   后果：从 Linux 套上这个状态起，Zephyr 写 GPIO0_A2 到不了引脚。压测时序里 50 s 那次拉低在 Linux
   起来之后，很可能根本没出现在引脚上。§11 的"Linux 未占用"不成立；§12 第 2 条的 DR 读数只能证明
   压测固件在跑，不能证明引脚电平。
   核对：Linux 下读 `0xff950000` [11:8] 和 `0xff910088` [6:0]。7 和 0x1f 就是被切走了，0 才是 GPIO。
   IOMUX 是 0 时，EXT_PORT（`devmem 0xff940070` bit2）读到的就是引脚实际电平。
   修法：`&pwm0_4ch_1 { status = "disabled"; }`。仓库 DTS 9/7 就有这条，SDK 一直没装。它和 fix A
   在同一个文件里，一起装、一起编 boot.img。
8. 【新发现 2026-09-25，仓库已改，未上板】**引脚核对：gpio_basic_api 的老环回脚是 SPI NAND 的数据线。**
   依据飞凌 `开发板手册/2-FET3506B-S引脚复用对照表-20251028.xlsx`（SoM 脚号、底板网络名、核心板上下拉、
   特殊说明）和板上 DTB（反编译后逐节点查 pinctrl、`*-gpios`、RM_IO 功能号，只算 okay 的节点）。
   全表和上板核对命令在 `SPI-TEST.md` 第 9 节。
   - gpio2 pin4/5 = GPIO2_A4/A5 = FSPI_D2/D3（SoM 116/113 脚）。对照表注明 GPIO2_A0～A5
     "与核心板SPI NAND FLASH引脚复用，只能在EMMC版本核心板使用"，本板是 NAND 版，DTB 里 spi-nand 按
     `spi-rx-bus-width = 4` 读。`rk3506_gpio_configure()` 把 pad 的 IOMUX 写成 0（GPIO）并改上下拉，
     所以不接跳线、光跑老版 gpio_basic_api 就会切走 NAND 的 D2/D3。9/3 那版 GPIO shell 调试固件
     （老 `enable_gpio2.overlay`，gpio2 全部放开）在 shell 里配 gpio2 的脚也一样。
     FSPI 节点没有 pinctrl，debugfs `pinmux-pins` 里这几根是 UNCLAIMED，看不出来，要读
     `0xff4d8044`（GPIO2A_IOMUX_SEL_1，低 16 位期望 0x11）。gpio2 其余的 B0～C0 是 Linux 的 RMII0，整组没有空脚。
   - 环回改到 GPIO3_A6/A7（SoM 129/128，底板普通 IO，核心板无上下拉，Linux 只有 disabled 的 SAI2 节点提到）。
     `tests/drivers/gpio/gpio_basic_api/boards/ok3506b_s12_amp_uart1.overlay` 改成 gpio3 6/7，
     `gpio-reserved-ranges = <0 6>, <8 7>`（GPIO3_A0～A5 是 TF 卡）。
   - GPIO 中断分组：Linux 的 gpio 节点只用第 0 组（SPI 0/4/8/12/16），板上 DTB 的 `amp-irqs` 把各 bank
     第 3 组（GIC 35/39/43/47/51）交给 CPU2，Linux 的 gpio-rockchip 不碰分组路由寄存器。Zephyr 用 GPIO
     中断必须设 `rockchip,irq-group = <3>`，默认 0 会和 Linux 抢同一个 SPI。新 overlay 已设。
     gpio_power_cycle 设的是 2，它不用中断，没影响。
   - `samples/rk3506/spi_loopback/gpio-cs-build.overlay` 的片选从 GPIO0_A3 改到 GPIO0_B5（SoM 66，底板普通 IO，
     Linux 没用）。A3 是 Linux 的 PWM0_CH2（`pwm@ff932000`，LCD 背光）。
   - `boards/debug/enable_gpio2.overlay` 改成 17 个脚全部 reserved；新增 `boards/debug/enable_gpio3.overlay`
     （只放出 A6/A7，irq-group 3）。pingpong CMakeLists 调试钩子注释里的示例路径同步改成 gpio3。
   - CAN0 在 GPIO0_B6/C0（RM_IO14/16，功能 0x1c/0x1d），底板网络名是 SPI1_CSN0/SPI0_CLK。CAN0 在用时
     SPI1 只能用 CSN1，SPI0 不能走引脚。GPIO0_C4 是网口 PHY 复位（ethernet `snps,reset-gpio`），
     老的 rm_io19/20（C3/C4）接法别再用。
   - 其余 Linux 占用（Zephyr 别碰）：GPIO0_A4/A5 I2C2（rx8010、pcf8563 两颗 RTC），A6 TF 卡检测，B3/B4 I2C0，
     C5 PWM0_CH0（CPU 电压），C6/C7 UART0（fiq-debugger，ttyFIQ0），D0 心跳灯；GPIO1_C1/C2/D0/D1 音频；
     GPIO3_A0～A5 TF 卡；GPIO4_B0/B1 启动脚（B1 还是 adc-keys）。RM_IO 功能号 15/16/19/20/30/31/32 已被 Linux 选走。
   - 编译验证：gpio_basic_api、spi_loopback + gpio-cs-build（加 `CONFIG_GPIO=y`）、pingpong +
     enable_gpio3 / enable_gpio2 + gpio-shell.conf 共 4 个，按 §4 模板在新的 /tmp 目录编过。
     zephyr.dts 里 reserved-ranges、irq-group、cs-gpios 都符合预期，警告只有既有的 `__rbit` 和 `GPIO_INT_MASK`。
     SPI 两版归档镜像不受影响：没开 GPIO 和 CAN，只碰 UART1 和 SPI1 的脚。
   - 建议未做：板级 DTS 给各 gpio 节点默认加 `gpio-reserved-ranges` 挡住 Linux 的脚，应用 overlay 忘写也抢不到。
   - 待用户回：以前有没有在板上跑过老版 gpio_basic_api，或在 9/3 调试固件的 shell 里动过 gpio2。
     有的话查 NAND（dmesg 里的 UBI/ECC 报错）。

**仓库外的改动【已镜像 2026-09-23】**：新增 `scripts/sdk-sync.sh`
（`status` / `diff` / `import` / `install`），清单：
`sdk/device/rockchip/.chips/ok3506/{amp_linux_zephyr.its,amp-zephyr.cfg}`、
`sdk/device/rockchip/common/scripts/mk-amp.sh`、
`linux/kernel-6.1/arch/arm/boot/dts/OK3506-S-MINI-common.dtsi`（含为 CAN0 让路而禁用
`&spi0`（RM_IO16/GPIO0_C0 撞 CAN0_RX）/`&spi1`（RM_IO14/GPIO0_B6 撞 CAN0_TX）的改动），
以及原有三个 dts 镜像；`install` 同时重建 `zephyr-rk3506-amp-link` 符号链接。
SDK 重同步后跑 `scripts/sdk-sync.sh install`；在 SDK 里改过文件后跑 `import`。
2026-09-25 重跑 `status`：其余 6 份镜像与 SDK 逐字节一致，符号链接正确。
唯一的 DIFFERENT 仍是 `OK3506-S-MINI_amp_nand.dts`：仓库版比 SDK 新，多了两处，SDK 都没装：
2026-09-07 的 `&pwm0_4ch_1 { status = "disabled"; }`（common.dtsi 里它是 okay 且占 rm_io2 = GPIO0_A2，
压测信号脚），和 2026-09-25 的 fix A（`&rockchip_amp` 多列 6 个时钟，见第 6 条）。
【2026-09-25 更正】这里原来写"SDK 现有 DTB 里 `pwm@ff171000` 本来就是 disabled，眼下板上无冲突"，
那是看错了节点。`pwm0_4ch_1` 是 `pwm@ff931000`，SDK 现有 DTB 里它是 okay，冲突是真的，见第 7 条。
**以后从 SDK 源码重编内核前先跑 `scripts/sdk-sync.sh install`**，
方向是仓库到 SDK，不要 `import`（会把这些改动冲掉）。
SPI 与 CAN0 的引脚取舍仍需板级决定：SPI1 的 CSN0 在 GPIO0_B6，本板给了 CAN0_TX。
SPI 测试只用 CSN1（GPIO0_A7）和 GPIO0_B0/B1/B2，CAN0 的引脚没动。
CAN0_RX 在 GPIO0_C0（底板 SPI0_CLK），所以 SPI0 也不能走引脚，只做内部回环。

**验证状态（2026-09-24）**：已编译验证，**未上板**。
- 按 §4 模板（系统 gcc 10.3.1）与 SDK `build_zephyr` 同参数（SDK 自带 gcc 10.2.1，
  BOARD/SOC/DTS_ROOT 走 `zephyr-rk3506-amp-link`）各编一遍 pingpong 与 gpio_power_cycle，
  全部通过。两个 `.config` 都是 `CONFIG_GIC_SAFE_CONFIG=y`，`RPMSG_MBOX_GIC_ROUTE` 已不存在。
- 警告：全部来自上游 `include/zephyr/arch/arm/bit_rev.h` 的 `__rbit` 隐式声明（既有），
  外加 gpio_power_cycle 里 `gpio_rk3506.c:23` 的 `GPIO_INT_MASK redefined`（驱动把它当寄存器
  偏移 0x018 用，gpio.h 内联函数在重定义前已展开，功能无影响，改名即可消除）。本次改的
  五个文件零警告。
- 反汇编确认第 1 项：`z_arm_mmu_init` 写 SCTLR 只 `orr` 0x1000 与 0x20000001，不带 C 位；
  `rk3506_apply_cache_policy` 在旧 C 位置位时先跑 `c7, c14, 2`（DCCISW）再写 SCTLR。
- 打包：`build.sh clean-amp` 被会话的自动模式拦截，SDK `output/` 未动（仍是 9/7 生产版）。
  改为在 /tmp 用 SDK 的 `rtos/bsp/rockchip/tools/mkimage -f amp.its -E -p 0xe00` 打包，ITS 按
  mk-amp.sh 同样剥掉 share/compile 块。等价性已验证：用归档的 9/7 zephyr-pingpong.bin 按此法
  重打，与 `amp-pingpong-production.img` 仅差 FDT 内存保留表里 8 个字节（mkimage 把自身 x86
  堆地址写了进去，随 ASLR 每次不同，官方流程两次打包也不同），其余逐字节一致。

新镜像（`~/rk3506/amp-images/`，均含工作树全部未提交改动，含 9/8 pinctrl/GPIO 重写与
9/22 CRU 重构，这两批此前未进过打包线）：

| 文件 | 应用 | zephyr.bin sha256 前缀 |
|---|---|---|
| `amp-pingpong-20260924-gicsafe.img` | rk3506_pingpong（RPMsg 生产版） | `78128fb6` |
| `amp-powercycle-gpio0a2-20260924.img` | gpio_power_cycle（压测） | `736207d9` |

上板复验：RKDevTool 烧 amp 分区，看 U-Boot 的 `AMP: Brought up cpu[f02]`，Linux 下
`/sys/bus/rpmsg/devices` 有 `virtio0.rpmsg-ap3-ch0.-1.12291` 且 `rk3506_rpmsg_char_ping` 通。
判定标准就是 ping 通。想看寄存器：Linux 下 `devmem 0xff5818b0 8`（GICD 0xff581000 +
ITARGETSR 0x800 + 176）应为 `0x04`。`compiled_flags`（BIT(6)=GIC_SAFE_CONFIG，BIT(1)=legacy
ROUTE）与 `gic_target_*` 只有 `tests/ztest/rk3506_amp_cache_stress` 的共享内存探针会导出，
pingpong 不打印；全树无代码对 BIT(1) 做断言。

**SPI 驱动整合（2026-09-25）**：已编译验证，**未上板**。
- 驱动 `zephyr/drivers/spi/spi_rk3506.c` + `Kconfig.rk3506` + binding
  `dts/bindings/spi/rockchip,rk3506-spi.yaml`。只做控制器模式，轮询收发，无中断无 DMA；
  8/16 位帧，模式 0～3，366 Hz～12 MHz（24 MHz OSC 偶数分频）。完整范围见
  `samples/rk3506/spi_loopback/README.md` 的"驱动范围"。
- 测试应用 `samples/rk3506/spi_loopback`，4 个变体：默认（SPI0、SPI1 都走内部回环）、
  `external-spi1.overlay`（SPI1 走引脚，要跳线）；`external-pins.overlay` 和 `gpio-cs-build.overlay`
  只做编译检查，不烧。
- 编法同上面 9/24 两版：SDK gcc 10.2.1，走 `zephyr-rk3506-amp-link`，/tmp 打包，SDK `output/` 未动。
  零新增告警，只有上面记过的 `__rbit`（4 个变体都有）和 `GPIO_INT_MASK`（只有 gpio-cs 变体开了 GPIO）。
- twister 没跑：venv 里缺 `natsort` 等模块，没装。4 个变体都用 cmake 单独编过。

| 文件 | 应用 | zephyr.bin sha256 前缀 |
|---|---|---|
| `amp-spi-loopback-20260925.img` | spi_loopback（内部回环，不接线） | `f86730e0` |
| `amp-spi1-external-20260925.img` | spi_loopback + external-spi1.overlay（SPI1 走引脚，跳 78↔76） | `c1808319` |

上板步骤、期望输出和要发回的东西都在仓库根目录 **`SPI-TEST.md`**。

**操作陷阱**（已踩过，见 §12）：ping 超时 ≠ CPU2 死；切 `RK_AMP_ZEPHYR_APP` 必须先
`./build.sh clean-amp`。

## 15. Codex 接手核验（2026-09-28 UTC / 2026-09-27 PDT）

### Claude Code 的真实停点

先读取了 Claude 的 RK3506 memory、本文档、`SDK-PACK.md`、`SPI-TEST.md`，再对照原始会话：
`~/.claude/projects/-home-kuro/23246b91-bc7e-4c80-8771-c476bc3ff14d.jsonl`。
最后一条实质性的工程总结在第 4511 行，时间为 `2026-09-25T14:44:15.838Z`：
引脚核对完成，四个 overlay 编译通过，未出新镜像，未提交。第 4515 行
`2026-09-26T07:38:50.864Z` 只是用户询问是否还在；未找到之后的上板反馈。
会话文件的 9/27 修改时间不能当作新增工程进度。

- U-Boot AMP 已烧写验证、GPIO 驱动曾上板可用，是用户此前确认的事实（§13/§14），
  不再列为“尚未跑通”的阻塞。
- SPI 驱动与测试已整合，9/25 两版镜像已归档，仍待板上测试。
- 9/24 的 GIC-safe pingpong、powercycle，以及改用 GPIO3_A6/A7 的 gpio_basic_api，
  都没有新增的硬件验证结果；本次构建不能替代这些结果。

### 本次实际核验

1. **仓库状态**：`main=cbb9882a627`，相对本地 `origin/main` 为 `0 behind / 4 ahead`；
   28 个已跟踪文件有修改、21 个文件未跟踪。未联网 fetch，未提交或推送。
2. **SDK 同步**：`scripts/sdk-sync.sh status/diff` 显示其余 6 份镜像文件和 symlink 均一致，
   仅 `OK3506-S-MINI_amp_nand.dts` 不一致。差异确实只有 PWM0_CH1 禁用和 AMP 额外持有的
   SPI0/SPI1/CAN0 六个时钟。没有执行 `install` 或 `import`。
3. **SDK 现存产物**：`kernel-6.1/zboot.img` 与 AMP DTB 的修改时间仍是 2026-06-03（PDT）。
   `fdtget` 读取现存 DTB：`/rockchip-amp/clocks` 为 48 字节（6 个时钟），
   `/pwm@ff931000/status=okay`，bootargs 无 `clk_ignore_unused`。
   这些是编译机上的 SDK 产物，未读取当前板上镜像，不能据此断定板子此刻烧的是哪一版。
4. **归档镜像**：两版 SPI amp.img 的完整 SHA-256 与 `SPI-TEST.md` §1 一致；
   `mkimage -l` 中固件大小均为 57864 字节、加载地址为 `0x03e00000`，
   声明的固件 SHA-256 分别与归档裸固件 `f86730e0…` / `c1808319…` 一致。
5. **源码核验**：SDK `drivers/irqchip/irq-gic.c:gic_dist_init()` 调用
   `irq-gic-common.c:gic_dist_config()`；后者对所有 SPI 写 ACTIVE_CLEAR/ENABLE_CLEAR，
   未排除 AMP IRQ。§14 第 5 条尚未修复，实际外设受影响的时序仍需板上验证。
6. **引脚配置**：gpio_basic_api 与 GPIO3 shell overlay 只放行 GPIO3_A6/A7、使用 IRQ group 3；
   GPIO2 shell overlay 保留全部 17 个脚；SPI GPIO-CS 编译变体使用 GPIO0_B5。
   本次未重新审阅硬件接线，沿用 Claude 已核对的飞凌表与 Linux 分配结论。

### 独立重编结果

在新的 `/tmp/rk3506-review-20260927-182236/` 目录逐目标配置并构建，
使用 SDK 自带 `arm-none-eabi-gcc 10.2.1`、Python 3.12.13、CMake 3.28.4，
`ZEPHYR_BASE` 和 BOARD/SOC/DTS_ROOT 显式指向当前仓库的 `zephyr/`，
通过 `EXTRA_ZEPHYR_MODULES` 指定树内 CMSIS 与 picolibc。

| 构建目录 | 应用 / 变体 | 结果 |
|---|---|---|
| `spi-internal` | `samples/rk3506/spi_loopback`，默认内部回环 | 编译链接通过 |
| `spi1-external` | 同上 + `external-spi1.overlay` | 编译链接通过 |
| `gpio-basic-api` | `tests/drivers/gpio/gpio_basic_api`，GPIO3_A6/A7 | 编译链接通过 |
| `rpmsg-pingpong` | `samples/subsys/ipc/rpmsg/rk3506_pingpong` | 编译链接通过 |

完整命令、状态和告警在上述临时目录的 `results.json`，日志为 `<构建目录>.log`。
告警仍包括已有的 `__rbit` 隐式声明、GPIO 构建中的 `GPIO_INT_MASK` 重定义；
直接构建还提示 subtree 不是独立 Git 仓库，需用 `BUILD_VERSION` 指定版本标识。
四个目标均完成编译链接，但不称为“零告警”或“上板通过”。`git diff --check` 通过。

Twister 仍缺 `natsort`、`ply`、`pytest`，本次未运行，也未改动 venv。
旧 BSP 布局检查脚本在当前 subtree 上报告缺少 `zephyr/module.yml`：脚本沿用旧独立
模块布局，当前实际构建以完整 Zephyr 源码树为基础，不能将此项直接当作构建失败。
脚本还报告没有 bindings，但实际 `dts/bindings/` 及 RK3506 binding 均存在；
后续需先适配检查器，不能靠补一个可能改变模块发现行为的 module.yml 消除提示。

### 建议的后续顺序

1. 收尾 Linux AMP 资源保护：已有的时钟/PWM DTS 修复需装入 SDK；GIC 的 AMP 中断
   使能保护还需实现，再生成带明确版本与备份的 boot.img。
2. 用新 Linux 镜像验证 SPI 内部回环、SPI1 引脚回环与 GPIO 中断，并复验 RPMsg 和 CAN。
   9/25 SPI 测试镜像、接线与验收项目继续使用 `SPI-TEST.md`；硬件实测不能由本次重编代替。
3. 将已核实的 GPIO 保留范围下沉到板级默认配置，整理未提交改动和测试证据后再提交。

本次只更新交接文档并做隔离构建核验，没有改动驱动/板级源码、SDK、归档镜像或板上固件。

## 16. 完整 update.img 构建（2026-09-28 UTC）

用户随后要求编译完整升级包。本节更新 §15 的交付状态，之前的历史记录保留。

- 归档：`~/rk3506/amp-images/update-20260928-020725Z-pingpong/update.img`。
- 大小：71,860,810 字节（68.53 MiB）。
- SHA-256：`ca6f7271357893a4cb57c157d811cfb9e2c59d4205ba375326f12c34d2e6b35a`。
- SDK 标准输出：`output/update/Image/update.img`，内容相同。
- 配置：`OK3506-S-MINI_amp_nand_zephyr_defconfig`；AMP 为默认 RPMsg pingpong，非 SPI 回环应用。

### 构建与修复

1. 先备份所有现存打包输入和旧 update.img 至归档的 `before/`，再 `scripts/sdk-sync.sh install`。
2. `./build.sh kernel` 成功，Linux 6.1.99 的有效 CONFIG 项与旧配置一致。新 DTB 中
   AMP clocks 为 96 字节 / 12 个时钟，`pwm@ff931000` 为 disabled；§14 第 6/7 条修复已进入产物。
3. 清理 AMP 缓存后首次 SDK 构建失败：真实路径的应用把 Zephyr 根追加为外部模块，
   却使用 symlink 形式的 ZEPHYR_BASE，导致模块校验失败。移除 pingpong CMake 中的自注册后，
   仍因真实路径/链接路径混用触发 CMake include 包装递归。
4. 最小修复：pingpong `CMakeLists.txt` 保留 BOARD/SOC/DTS_ROOT 胶水，删除无效的
   `EXTRA_ZEPHYR_MODULES` 自注册；仓库镜像的 `sdk/.../mk-amp.sh:amp_resolve_path()`
   改用 `realpath -m` 统一路径，再同步到 SDK。实际 SDK 构建通过。
5. 新 amp.img 为 103424 字节，内嵌 zephyr.bin 为 98660 字节，SHA-256
   `9d7f5a072f520c3dba9a72c20486b79f3db149d4a8199ef5f97d67c5a37cd76e`。
   CONFIG_GIC_SAFE_CONFIG / PICOLIBC / CAN / RPMSG_LITE / PRIVATE_ICACHE / PRIVATE_DCACHE 均为 y。
6. `./build.sh updateimg` 成功；沿用现有带 AMP 的 U-Boot、loader、rootfs、oem、userdata 和分区表。
   U-Boot 与 2026-09-08 归档一致，rootfs 模块目录版本同为 6.1.99。

### 验证与限制

- 从 boot/amp FIT 提取 DTB/固件，与新编输入做哈希核对；时钟/PWM 修复和应用配置均符合预期。
- 厂商 rkImageMaker、afptool 两层回拆成功；全部 9 个打包项与输入 SHA-256 一致，固定分区均未超限。
- 本包未上板，Linux GIC 清 AMP IRQ 使能问题仍未修；GPIO/CAN 中断验收仍待完成。
- 本包含 userdata.img，是完整包。旧镜像、构建日志、校验结果及说明见归档目录。
- SDK 的 `build.sh:run_hooks()` 存在失败返回值被 `if !` 取反吞掉的既有问题：
  本次失败的 AMP hook 外层仍返回 0。未扩大修改 SDK 主脚本；构建成功以新产物存在、
  时间/内容校验及回拆结果为准，不能只看 build.sh 的退出码。
- 已更新 SDK 镜像文件与仓库构建胶水；未修改 Linux GIC 或 Zephyr 外设驱动，未提交/推送或烧板。
