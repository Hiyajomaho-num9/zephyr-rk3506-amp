# RK3506 AMP / RPMsg audit - 2026-05-05

> Historical note, 2026-05-16:
> this document records the Phase 4 audit state.  Paths that mention the old
> staging tree are historical evidence only.  The active Zephyr tree is now
> `zephyr-rk3506/`, with the production sample at
> `zephyr-rk3506/samples/rk3506/amp_pingpong`.

## 0. 结论

当前 OK3506-S-MINI AMP 链路不是 Linux `remoteproc + resource table` 模式。

实际链路是：

1. U-Boot 从 AMP FIT 的 `amp2` loadable 把 CPU2 固件搬到 `0x03e00000`，通过 Rockchip AMP/OP-TEE 路径拉起 CPU2。
2. Linux 只保留 CPU0/CPU1，DTS 删除 `cpu@f02`。
3. Linux `rockchip_amp` 负责保护/配置 AMP 侧资源：CPU affinity、IRQ route、UART1 clock/pinmux 等。
4. Linux `rockchip-rpmsg` 直接注册一个静态 VirtIO RPMsg device；vring 固定在 `0x03c00000`，buffer coherent pool 固定来自 `0x03d00000`。
5. RT-Thread 默认 `ok3506_amp_uart1_defconfig` 是 UART1 console 方案，未启用 RPMsg-Lite；但 RK3506 的 RPMsg-Lite 平台代码已经在 RTOS 树里。
6. 当前 Zephyr clean baseline 已经能在 CPU2 跑、UART1 打印、shell/heartbeat 工作，但还没有共享内存/RPMsg 代码。

因此下一步不要直接做完整 RPMsg。先做 `0x03b00000` 共享内存探针，确认 CPU2 写入能被 Linux 稳定看见，再进入 RPMsg。

## 1. 证据来源

### 文档侧

- Vendor AMP guide 摘要：Rockchip AMP 通信是“中断 + 共享内存”，支持 Mailbox/softirq/SGI；Linux shared memory 仅支持 uncache。
- Vendor AMP guide 指向 Linux mailbox RPMsg 代码：`kernel/drivers/rpmsg/rockchip_rpmsg_mbox.c`。
- Vendor AMP guide 指向 RTOS RPMsg-Lite 平台代码：`rtos/bsp/rockchip/common/drivers/rpmsg-lite/.../platform/RKXX/rpmsg_platform.c`。

### SDK 侧

- Linux DTS:
  - `kernel-6.1/arch/arm/boot/dts/rk3506-amp.dtsi`
  - `kernel-6.1/arch/arm/boot/dts/OK3506-S-MINI_amp_nand.dts`
- Linux drivers:
  - `kernel-6.1/drivers/rpmsg/rockchip_rpmsg_mbox.c`
  - `kernel-6.1/drivers/rpmsg/virtio_rpmsg_bus.c`
  - `kernel-6.1/drivers/rpmsg/rpmsg_ns.c`
  - `kernel-6.1/drivers/rpmsg/rockchip_rpmsg_test.c`
  - `kernel-6.1/drivers/soc/rockchip/rockchip_amp.c`
- RTOS:
  - `rtos/bsp/rockchip/rk3506-32/board/evb1/ok3506_amp_uart1_defconfig`
  - `rtos/bsp/rockchip/rk3506-32/gcc_arm.ld.S`
  - `rtos/bsp/rockchip/rk3506-32/board/common/board_base.c`
  - `rtos/bsp/rockchip/common/drivers/rpmsg-lite/lib/include/platform/RK3506/`
  - `rtos/bsp/rockchip/common/drivers/rpmsg-lite/lib/rpmsg_lite/porting/platform/RK3506/rpmsg_platform.c`
  - `rtos/bsp/rockchip/common/tests/rpmsg_test.c`
- Current Zephyr:
  - `zephyr-clean/BASELINE-2026-05-05.md`
  - `zephyr-clean/soc/rockchip/rk3506/soc.c`
  - `zephyr-clean/apps/amp_uart1/src/main.c`

## 2. 启动与资源归属

### 2.1 U-Boot / AMP FIT 拉起 CPU2

Zephyr AMP FIT:

- `device/rockchip/.chips/ok3506/amp_linux_zephyr.its:12-23`
  - image name: `amp2`
  - description: `zephyr-core2`
  - `cpu = <0xf02>`
  - `thumb = <0>`
  - `load = <0x03e00000>`
  - `srambase = <0xfff80000>`
  - `sramsize = <0x0000c000>`
- `device/rockchip/.chips/ok3506/amp_linux_zephyr.its:24-34`
  - build metadata 指向 `zephyr-clean/apps/amp_uart1`
  - board `ok3506_amp_uart1`
  - Zephyr base `/home/kuro/zephyr-work/zephyr`

RTT 原始 AMP FIT 相同位置：

- `device/rockchip/.chips/ok3506/amp_linux.its:12-29`
  - image name: `amp2`
  - data `rtt2.bin`
  - `cpu = <0xf02>`
  - `load = <0x03e00000>`
  - config `board/evb1/ok3506_amp_uart1_defconfig`

构建脚本会解析 ITS 的 `share` 节，然后导出给 RTOS/Zephyr 构建：

- `device/rockchip/common/build-hooks/25-amp.sh:447-461`
  - 读取 `share { ... }`
  - 导出 `LINUX_RPMSG_BASE`
  - 导出 `LINUX_RPMSG_SIZE`
- `device/rockchip/common/build-hooks/25-amp.sh:467-472`
  - 生成最终 `amp.its` 时删除 `share` 和 `compile`
  - 用 `mkimage` 打包 `amp.img`

也就是说，`share` 是构建时信息，不是最终 FIT 运行时节点。

### 2.2 Linux 不拥有 CPU2

`rk3506-amp.dtsi` 明确删除 CPU2：

- `kernel-6.1/arch/arm/boot/dts/rk3506-amp.dtsi:10-13`
  - `/cpus/delete-node/ cpu@f02`

Linux 当前 `.config`：

- `kernel-6.1/.config:4635`
  - `CONFIG_REMOTEPROC` 未启用

所以 Linux 不通过 `remoteproc` 加载 CPU2 固件，也不解析 resource table。

### 2.3 Linux `rockchip_amp` 只做资源保护/配置

`rockchip_amp` DTS：

- `kernel-6.1/arch/arm/boot/dts/rk3506-amp.dtsi:15-38`
  - `compatible = "rockchip,amp"`
  - 默认保护/打开 HCLK_M0、STCLK_M0、UART4、TIMER 等资源
  - 默认 IRQ route 到 CPU2
- `kernel-6.1/arch/arm/boot/dts/OK3506-S-MINI_amp_nand.dts:90-108`
  - 板级覆盖为 UART1
  - UART1 IRQ 67 route 到 CPU2
  - MAILBOX IRQ 176 route 到 CPU2

`rockchip_amp` driver：

- `kernel-6.1/drivers/soc/rockchip/rockchip_amp.c:671-679`
  - probe 时 bulk enable DTS 中列出的 clocks
- `kernel-6.1/drivers/soc/rockchip/rockchip_amp.c:566-619`
  - 读取 `amp-irqs`
  - 记录各 IRQ 的 priority / affinity / cpumask
- `kernel-6.1/drivers/soc/rockchip/rockchip_amp.c:707-713`
  - 如果 DTS 有 `amp-cpus`，才通过 SMC boot CPU
  - 当前 `rk3506-amp.dtsi` 没有这个节点；CPU2 已由 U-Boot AMP FIT 拉起

## 3. 内存布局

| 区域 | 地址 | 大小 | DTS / ITS 来源 | 当前用途 |
|---|---:|---:|---|---|
| `amp-shmem` | `0x03b00000` | `0x00100000` | `OK3506-S-MINI_amp_nand.dts:51-55` | 通用 AMP 共享内存；Linux `rockchip-rpmsg` 不直接用。适合下一轮探针。 |
| `rpmsg` | `0x03c00000` | `0x00100000` | `OK3506-S-MINI_amp_nand.dts:57-61` | RPMsg vring reserved no-map。 |
| `rpmsg@3c00000 reg` | `0x03c00000` | `0x00020000` | `rk3506-amp.dtsi:40-48` | Linux `rockchip-rpmsg` 平台资源；vdev0 实际只用前 `0x10000`。 |
| `rpmsg-dma` | `0x03d00000` | `0x00100000` | `OK3506-S-MINI_amp_nand.dts:63-68` | Linux RPMsg coherent buffer pool。 |
| `amp` firmware | `0x03e00000` | `0x00100000` | `OK3506-S-MINI_amp_nand.dts:76-80`, ITS load | CPU2 固件运行区。 |
| MCU SRAM | `0xfff80000` | `0x0000c000` | `OK3506-S-MINI_amp_nand.dts:70-74`, ITS srambase/sramsize | AMP SRAM scratch / vendor AMP 参数区。 |

RTOS 构建侧：

- `rtos/bsp/rockchip/rk3506-32/build.sh:34-37`
  - `RTT_SHMEM_BASE=0x03b00000`
  - `RTT_SHMEM_SIZE=0x00100000`
  - `LINUX_RPMSG_BASE=0x03c00000`
  - `LINUX_RPMSG_SIZE=0x00200000`
- `rtos/bsp/rockchip/rk3506-32/gcc_arm.ld.S:12-14`
  - 定义 `LINUX_RPMSG` memory region
- `rtos/bsp/rockchip/rk3506-32/gcc_arm.ld.S:122-128`
  - 产生 `__linux_share_rpmsg_start__`
  - 产生 `__linux_share_rpmsg_end__`
- `rtos/bsp/rockchip/rk3506-32/board/common/board_base.c:166-168`
  - 把 `LINUX_RPMSG_BASE..BASE+SIZE-1` 映射为 `UNCACHED_MEM`

关键点：

- `0x03b00000` 是最安全的第一轮共享内存探针区。
- `0x03c00000` 和 `0x03d00000` 会被 Linux RPMsg driver 使用，探针阶段不要随便写。
- Linux DTS `no-map` 避免 Linux 普通内存分配污染这些区域，但 `/dev/mem` / `devmem` 仍可用于验证。

## 4. Linux RPMsg 现状

### 4.1 DTS 节点

`kernel-6.1/arch/arm/boot/dts/rk3506-amp.dtsi:40-51`：

```dts
rpmsg@3c00000 {
    compatible = "rockchip,rpmsg";
    mbox-names = "rpmsg-rx", "rpmsg-tx";
    mboxes = <&mailbox0 0 &mailbox2 0>;
    rockchip,vdev-nums = <1>;
    rockchip,link-id = <0x02>;
    reg = <0x3c00000 0x20000>;
    memory-region = <&rpmsg_dma_reserved>;
};
```

含义：

- CPU2 link id 是 `0x02`。
- Linux RX mailbox channel: `mailbox0 0`。
- Linux TX mailbox channel: `mailbox2 0`。
- vdev 数量是 1。
- vring platform resource 从 `0x03c00000` 开始。
- RPMsg buffer pool 来自 `rpmsg-dma@3d00000`。

### 4.2 Kernel config

当前 `kernel-6.1/.config`：

- `CONFIG_MAILBOX=y`
- `CONFIG_REMOTEPROC` 未启用
- `CONFIG_RPMSG=y`
- `CONFIG_RPMSG_NS=y`
- `CONFIG_RPMSG_ROCKCHIP_MBOX=y`
- `CONFIG_RPMSG_VIRTIO=y`
- `CONFIG_RPMSG_CHAR` 未启用
- `CONFIG_RPMSG_CTRL` 未启用
- `CONFIG_RPMSG_ROCKCHIP_TEST` 未启用
- `CONFIG_RPMSG_TTY` 未启用

影响：

- Linux 会创建 RPMsg host。
- Linux 会接收 remote 的 name-service announcement。
- 但当前没有通用 userspace `/dev/rpmsg_ctrl*` / `/dev/rpmsg*` 接口。
- 也没有内置 Rockchip ping-pong test driver。

如果后面要 Linux userspace 直接 `ping/pong`，必须额外启用 `CONFIG_RPMSG_CHAR/CTRL`，或者启用/改造 `CONFIG_RPMSG_ROCKCHIP_TEST`，或者写一个很小的 kernel rpmsg client driver。

### 4.3 Vring 布局

Linux constants:

- `kernel-6.1/include/linux/rpmsg/rockchip_rpmsg.h:19-37`
  - payload size `496`
  - buffer size `512`
  - buffer count `64`
  - mailbox magic `0x524D5347`
  - vring align `0x1000`
  - vring size `0x8000`
  - one vdev overhead `0x10000`

Linux vring address calculation:

- `kernel-6.1/drivers/rpmsg/rockchip_rpmsg_mbox.c:307-340`
  - `vring[0] = reg.start`
  - `vring[1] = reg.start + RPMSG_VRING_SIZE`
  - next vdev starts at `start += 2 * RPMSG_VRING_SIZE`

所以当前 vdev0：

| queue | Linux name | physical address | size |
|---|---|---:|---:|
| vq0 | input / rvq | `0x03c00000` | `0x8000` |
| vq1 | output / svq | `0x03c08000` | `0x8000` |

这和启动日志一致：

```text
rockchip-rpmsg 3c00000.rpmsg: rpdev vdev0: vring0 0x3c00000, vring1 0x3c08000
virtio_rpmsg_bus virtio0: rpmsg host is online
```

### 4.4 Linux 初始化顺序

`rockchip_rpmsg_mbox.c`：

- `:343-431` probe:
  - request `rpmsg-rx`
  - request `rpmsg-tx`
  - read `rockchip,link-id`
  - read `rockchip,vdev-nums`
  - set vring physical addresses
  - attach reserved DMA pool
  - register virtio device
- `:167-217` find_vq:
  - `ioremap()` vring physical memory
  - `memset_io()` 清空 vring
  - `vring_new_virtqueue()`

`virtio_rpmsg_bus.c`：

- `kernel-6.1/drivers/rpmsg/virtio_rpmsg_bus.c:893-899`
  - 期待两个 virtqueue，顺序为 `input`, `output`
- `:905-918`
  - vring size 小于 256 时，buffer 总数等于 `vring_size * 2`
  - 当前 `64 * 2 = 128` 个 512-byte buffer
  - coherent buffer 从 parent device 分配，即 `rpmsg-dma@3d00000`
- `:933-943`
  - Linux 先把 RX buffers 加到 rvq
- `:956-979`
  - 如果支持 `VIRTIO_RPMSG_F_NS`，创建 name-service endpoint
- `:985-998`
  - `virtio_device_ready()`
  - notify remote
  - 打印 `rpmsg host is online`

`rockchip_rpmsg_mbox.c` 的 feature 固定为 name service：

- `kernel-6.1/drivers/rpmsg/rockchip_rpmsg_mbox.c:281-284`
  - 返回 `RPMSG_VIRTIO_RPMSG_F_NS`

结论：

- Linux 是 host/master 侧。
- Linux 负责清 vring、放 RX buffer、分配 DMA buffers。
- remote 侧必须按 RPMsg-Lite remote 方式接入，不能自己当 master 重建 vring。

### 4.5 Mailbox 通知语义

Mailbox magic：

- `RPMSG_MBOX_MAGIC = 0x524D5347`

Linux TX notify：

- `kernel-6.1/drivers/rpmsg/rockchip_rpmsg_mbox.c:113-165`
  - `cmd = link_id & 0xff`
  - `data = RPMSG_MBOX_MAGIC`
  - 通过 `rpmsg-tx` mailbox 发送

Linux RX callback：

- `kernel-6.1/drivers/rpmsg/rockchip_rpmsg_mbox.c:84-105`
  - 收到 mailbox msg 后检查 magic
  - 设置 `RPMSG_REMOTE_IS_READY`
  - `vring_interrupt(0, vq[0])`

Linux TX complete callback：

- `kernel-6.1/drivers/rpmsg/rockchip_rpmsg_mbox.c:59-82`
  - 处理 consumed-buffer notification
  - `vring_interrupt(0, vq[1])`

注意：`RPMSG_REMOTE_IS_READY` 目前只被设置，没有在 `rockchip_rpmsg_mbox.c` 里作为发送前硬 gate 使用。真正 RPMsg 生命周期主要由 virtio/rpmsg core 和 vring notify 驱动。

### 4.6 Channel / endpoint 匹配

Linux name service:

- `kernel-6.1/drivers/rpmsg/rpmsg_ns.c:30-69`
  - remote 发 NS announcement
  - Linux 用 `msg->name` 创建 channel
  - `chinfo.dst = msg->addr`

Rockchip test driver:

- `kernel-6.1/drivers/rpmsg/rockchip_rpmsg_test.c:93-96`
  - 只匹配 `rpmsg-ap3-ch0`
  - 只匹配 `rpmsg-mcu0-test`
- 但当前 `CONFIG_RPMSG_ROCKCHIP_TEST` 未启用。

所以如果 Zephyr 现在发 NS announcement，Linux 最多会创建 channel；没有 client driver 时，不会自动出现一个可直接交互的用户态节点。

## 5. RT-Thread / RPMsg-Lite 现状

### 5.1 OK3506 默认 RTT AMP 配置没有启用 RPMsg

`rtos/bsp/rockchip/rk3506-32/board/evb1/ok3506_amp_uart1_defconfig`：

- `:81-84`
  - 启用 RT-Thread console
  - console device 是 `uart1`
- 对 `RT_USING_RPMSG_LITE` / `RT_USING_LINUX_RPMSG` / `RT_USING_COMMON_TEST_LINUX_RPMSG_LITE` grep 无命中。

RPMsg-Lite Kconfig 默认关闭：

- `rtos/bsp/rockchip/common/drivers/Kconfig:789-799`
  - `RT_USING_RPMSG_LITE` default `n`
  - `RT_USING_LINUX_RPMSG` 依赖 `RT_USING_RPMSG_LITE`，default `n`

因此原厂 OK3506 UART1 AMP RTT 示例主要是 console/finsh/msh，不是 RPMsg 示例。

### 5.2 RK3506 RPMsg-Lite 平台代码已经存在

RK3506 RPMsg-Lite constants:

- `rtos/bsp/rockchip/common/drivers/rpmsg-lite/lib/include/platform/RK3506/rpmsg_config.h:8-24`
  - payload `496`
  - buffer count `64`
  - endpoint size `512`
  - 使用 mailbox
  - mailbox magic `0x524D5347`
- `rtos/bsp/rockchip/common/drivers/rpmsg-lite/lib/include/platform/RK3506/rpmsg_platform.h:10-35`
  - vring align `0x1000`
  - vring size `0x8000`
  - link id: high 4 bit master, low 4 bit remote
  - `RL_PLATFORM_SET_LINK_ID(_M, _R)`

这些值和 Linux `rockchip_rpmsg.h` 对齐。

RK3506 RPMsg-Lite mailbox 行为：

- `rtos/bsp/rockchip/common/drivers/rpmsg-lite/lib/rpmsg_lite/porting/platform/RK3506/rpmsg_platform.c:28-35`
  - master uses `RL_MBOX_A2B`
  - remote uses `RL_MBOX_B2A`
- `:57-75`
  - remote callback 第一次 mailbox 触发 vq0，之后触发 vq1
- `:188-206`
  - `platform_notify()` 发送 `cmd=link_id`, `data=RL_RPMSG_MAGIC`

地址转换/cache：

- `:322-344`
  - map/cache 函数是 dummy
- `:352-365`
  - `vatopa/patova` 是 identity mapping

这要求共享内存在 RTOS MMU 中已经按 uncached/identity 映射。RK3506 RTT 侧确实这样做：

- `rtos/bsp/rockchip/rk3506-32/board/common/board_base.c:166-168`
  - `LINUX_RPMSG_BASE..+SIZE` 映射为 `UNCACHED_MEM`

### 5.3 RTOS Linux-RPMsg test 是可参考代码，但不应原样照搬

`rtos/bsp/rockchip/common/tests/rpmsg_test.c:346-476`：

- `:353-364`
  - 注释写的是 CPU0 master / CPU3 remote
  - HAL_AP_CORE 下 endpoint id 固定 `0x3003`
  - channel name 固定 `rpmsg-ap3-ch0`
- `:455-463`
  - `rpmsg_lite_remote_init(RPMSG_LINUX_MEM_BASE, RL_PLATFORM_SET_LINK_ID(master_id, remote_id), RL_NO_FLAGS)`
  - `rpmsg_lite_wait_for_link_up()`
  - 创建 endpoint
  - `rpmsg_ns_announce()`
- `:466-472`
  - 收 Linux 消息并回复 `"Rockchip rpmsg linux test!"`

这段代码证明 remote 侧应使用 `rpmsg_lite_remote_init()`，但 channel 命名/endpoint id 带 CPU3 历史包袱。给 RK3506 CPU2 做 Zephyr 时，不建议原样使用 `rpmsg-ap3-ch0`，除非同时启用并复用 Linux 的 `rockchip_rpmsg_test`。

## 6. 当前 Zephyr 现状

已经验证：

- `zephyr-clean/BASELINE-2026-05-05.md:3-12`
  - Zephyr v4.4.0 可通过 AMP FIT 跑在 CPU2
  - UART1 可打印
- `zephyr-clean/apps/amp_uart1/src/main.c:12-18`
  - main reached
  - heartbeat
- `zephyr-clean/apps/amp_uart1/prj.conf:8-18`
  - 已启用 Zephyr shell，serial backend polling

Zephyr SoC 侧已做：

- `zephyr-clean/soc/rockchip/rk3506/soc.c:45-55`
  - 清 `SCTLR.V`
  - 设置 `VBAR`
- `zephyr-clean/soc/rockchip/rk3506/soc.c:126-144`
  - early init UART1
  - reset hook 调整 I/C/A bits
- `zephyr-clean/soc/rockchip/rk3506/soc.c:151-183`
  - MMU 映射 vectors/GIC/RMIO/GPIO0_IOC/CRU/UART1

尚未做：

- 没有映射 `0x03b00000` / `0x03c00000` / `0x03d00000`。
- 没有 mailbox driver。
- 没有 VirtIO/RPMsg/OpenAMP 对接。

## 7. 下一轮：共享内存探针设计

目标只验证 CPU2 与 Linux 是否能稳定共享一块 uncached memory，不碰 RPMsg vring。

### 7.1 Zephyr 写入地址

默认只写：

- `0x03b00000`，即 `amp-shmem@3b00000`

不要默认写：

- `0x03c00000`：Linux RPMsg vring 区，Linux 会 `memset_io()` 和维护 vring。
- `0x03d00000`：Linux coherent buffer pool，Linux RPMsg core 会分配使用。

### 7.2 建议探针结构

起始地址：`0x03b00000`

```c
struct rk3506_amp_shmem_probe {
    uint32_t magic;        /* 0x524b5a50: "RKZP" */
    uint32_t version;      /* 1 */
    uint32_t counter;
    uint32_t uptime_ms_lo;
    uint32_t uptime_ms_hi;
    uint32_t last_status;  /* 0 = ok */
    uint32_t inverse_counter;
    uint32_t reserved;
};
```

Zephyr 每秒更新：

- `magic`
- `counter`
- `uptime_ms_lo/hi`
- `inverse_counter = ~counter`
- 写后做 memory barrier

### 7.3 Zephyr MMU 映射建议

先只映射 4KB 或 64KB：

```c
MMU_REGION_FLAT_ENTRY("amp-shmem-probe",
                      0x03b00000,
                      0x1000,
                      MT_DEVICE | MPERM_R | MPERM_W)
```

如果后续要扩大为完整 1MB，可以再改。探针阶段小映射更容易审计。

### 7.4 Linux 验证命令

板上 Linux：

```sh
devmem 0x03b00000 32
devmem 0x03b00004 32
devmem 0x03b00008 32
devmem 0x03b0000c 32
devmem 0x03b00010 32
devmem 0x03b00018 32
```

如果没有 `devmem`，写一个最小 `/dev/mem` reader。不要先引入 RPMsg，否则 debug 变量会变多。

### 7.5 验收标准

通过条件：

- UART1 heartbeat 继续正常。
- Linux `devmem 0x03b00000 32` 返回 `0x524b5a50`。
- `counter` 每秒递增。
- `inverse_counter == ~counter`。
- Linux dmesg 无新增 reserved memory / rpmsg fault。

失败时只排查三类：

1. Zephyr 没映射或写入 fault。
2. Zephyr 写了 cacheable memory，Linux 看不到最新值。
3. Linux `/dev/mem` 工具访问方式有问题。

## 8. 再下一轮：最小 RPMsg endpoint 方向

前提：共享内存探针通过。

### 8.0 参考树 `rk3506/amp/OK3506_Linux_Source` 的实锤对照

用户补充的参考树路径：

```text
/home/kuro/rk3506/amp/OK3506_Linux_Source
```

这个树比当前 clean Zephyr 树更接近原厂/飞凌的 Linux+RTOS AMP 方案，后续 RPMsg 适配应优先对照它，而不是继续猜。

关键证据：

| 文件 | 结论 |
|---|---|
| `device/rockchip/.chips/ok3506/amp_linux.its` | `amp2` 装载 `rtt2.bin` 到 `0x03e00000`，`share.rpmsg_base=<0x03c00000>`，`share.rpmsg_size=<0x00200000>`。 |
| `kernel-6.1/arch/arm/boot/dts/ok3506-amp-rtt.dtsi` | `rpmsg@3c00000` 使用 `mboxes = <&mailbox0 0 &mailbox2 0>`，`rockchip,link-id=<0x02>`，`reg=<0x03c00000 0x20000>`，`memory-region=<&rpmsg_dma_reserved>`。 |
| `kernel-6.1/arch/arm/boot/dts/ok3506-amp-rtt.dtsi` | reserved-memory：`amp-shmem=0x03b00000/1M`，`rpmsg=0x03c00000/1M`，`rpmsg-dma=0x03d00000/1M`，`amp=0x03e00000/8M`。 |
| `rtos/bsp/rockchip/ok3506-32/build.sh` | RT-Thread CPU2 构建时导出 `RTT_SHMEM_BASE=0x03b00000`、`LINUX_RPMSG_BASE=0x03c00000`、`LINUX_RPMSG_SIZE=0x00200000`、`CUR_CPU=2`。 |
| `rtos/bsp/rockchip/ok3506-32/gcc_arm.ld.S` | 非 SMP 时定义 `LINUX_RPMSG` 段，`.linux_share_rpmsg` 从 `LINUX_RPMSG_BASE` 开始，占满 `LINUX_RPMSG_SIZE`，并导出 `__linux_share_rpmsg_start__` / `__linux_share_rpmsg_end__`。 |
| `rtos/bsp/rockchip/ok3506-32/board/common/board_base.c` | RT-Thread MMU 把 `LINUX_RPMSG_BASE..+SIZE-1` 映射为 `UNCACHED_MEM`。 |
| `rtos/bsp/rockchip/ok3506-32/rtconfig.h` | 这个参考构建启用了 `RT_USING_RPMSG_LITE`、`RT_USING_LINUX_RPMSG`、`RT_USING_COMMON_TEST_LINUX_RPMSG_LITE`。 |
| `rtos/bsp/rockchip/common/tests/rpmsg_test.c` | Linux RPMsg remote 端使用 `rpmsg_lite_remote_init((void *)RPMSG_LINUX_MEM_BASE, RL_PLATFORM_SET_LINK_ID(0, remote_id), RL_NO_FLAGS)`，CPU2 时 link id 即 `0x02`。 |
| `rtos/bsp/rockchip/common/drivers/rpmsg-lite/lib/include/platform/RK3506/rpmsg_platform.h` | `VRING_ALIGN=0x1000`，`VRING_SIZE=0x8000`，`RL_VRING_OVERHEAD=2*VRING_SIZE`，`RL_PLATFORM_SET_LINK_ID(_M,_R)=((_M<<4)&0xf0)\|(_R&0xf)`。 |
| `rtos/bsp/rockchip/common/drivers/rpmsg-lite/lib/include/platform/RK3506/rpmsg_config.h` | `RL_BUFFER_PAYLOAD_SIZE=496`，`RL_BUFFER_COUNT=64`，mailbox magic `RL_RPMSG_MAGIC=0x524D5347`，启用 `RL_PLATFORM_USING_MBOX`。 |
| `rtos/bsp/rockchip/common/drivers/rpmsg-lite/lib/rpmsg_lite/porting/platform/RK3506/rpmsg_platform.c` | remote 收 mailbox 后第一次 notify `vq0`，后续 notify `vq1`；发送 notify 时用 mailbox DATA=`0x524D5347`，CMD=link id。 |

由此修正前面的判断：

- `0x03c00000` 不是只给 `0x20000`，Linux platform `reg` 只暴露前 `0x20000` 给 vring，但 RT-Thread 端把 `0x03c00000..0x03dfffff` 整个 2MB 都作为 `LINUX_RPMSG` uncached 映射/链接段。
- Linux DTS 仍单独把 `0x03d00000..0x03dfffff` 声明成 `rpmsg-dma` shared-dma-pool。也就是说 RT-Thread 端的 `LINUX_RPMSG_SIZE=0x00200000` 覆盖了 Linux vring 区和 Linux rpmsg-dma buffer pool 两段。
- Zephyr 后续如果走 RPMsg-Lite 兼容路线，也应映射 `0x03c00000..0x03dfffff` 为 device/uncached，而不是只映射 `0x03c00000..0x03c1ffff`。
- 共享内存探针仍然只写 `0x03b00000` 是正确的，因为这轮不能污染 Linux RPMsg vring/buffer。

### 8.1 Linux 侧先选测试入口

当前 kernel 没有 userspace RPMsg 接口。三种可选路线：

1. 启用 `CONFIG_RPMSG_ROCKCHIP_TEST`
   - 复用 `rockchip_rpmsg_test.c`
   - remote channel name 用 `rpmsg-ap3-ch0` 或同步改 Linux driver name
   - 优点：最接近原厂 test
   - 缺点：名字带 CPU3 历史包袱，不适合长期方案
2. 启用 `CONFIG_RPMSG_CHAR` + `CONFIG_RPMSG_CTRL`
   - userspace 更方便
   - 需要确认这个 6.1 Rockchip kernel 的 rpmsg_char 与当前 virtio rpmsg core 配套情况
3. 写一个极小 Linux rpmsg client driver
   - match `"rk3506-zephyr-ping"`
   - probe 后发 ping，callback 打印 pong
   - 最干净、可审计，但要多维护一个内核小模块/patch

建议：第一个 RPMsg bring-up 用路线 3，避免 userspace/char 变量；跑通后再决定是否要 rpmsg_char。

### 8.2 Zephyr 侧需要实现/移植的最小集合

必须对齐 Linux：

- link id: `0x02`
- mailbox magic: `0x524D5347`
- vring0: `0x03c00000`
- vring1: `0x03c08000`
- vring size: `0x8000`
- vring align: `0x1000`
- buffer size: `512`
- payload: `496`
- buffer count per direction: `64`
- Linux coherent buffer physical addresses来自 `0x03d00000..0x03dfffff`

Zephyr 侧实现路线：

1. 优先评估 Zephyr/OpenAMP 是否能用静态 shared memory + custom mailbox notify。
2. 如果 OpenAMP 绑定成本太高，再参考 RTOS RPMsg-Lite RK3506 port 写最小 remote。
3. 不要手写大段 VirtIO/RPMsg，除非 OpenAMP/RPMsg-Lite 都证明不合适。

### 8.3 RPMsg 验收标准

第一版只做：

- Linux -> Zephyr: `ping`
- Zephyr -> Linux: `pong`
- Zephyr UART1 同时打印收到的 src/dst/len/counter
- Linux dmesg 打印 channel create/probe/callback

不要一开始做 shell over rpmsg、bulk transfer、DMA 压测。

## 9. 当前 open questions

1. Zephyr mailbox 驱动：
   - 需要按 RK3506 TRM Mailbox 章节和 Linux mailbox driver 核对寄存器。
   - 不要直接猜 `mailbox0/mailbox2` 寄存器方向。
2. Linux RPMsg 测试入口：
   - 需要决定启用 test/char/ctrl 还是写小 client driver。
3. Zephyr shared memory 属性：
   - 探针阶段应先按 device/uncached 映射，确认可见性。
   - 后续 RPMsg 可以再优化 cache 策略。
4. `amp-shmem@3b00000` 的既有用途：
   - 当前 Linux `rockchip-rpmsg` 不直接用它。
   - 但后续如果启用其他 AMP feature，需要避免冲突。

## 10. 建议的下一步命令级任务

下一轮只改 Zephyr clean tree：

1. 在 `zephyr-clean/soc/rockchip/rk3506/soc.c` 加 `0x03b00000` MMU region。
2. 在 `zephyr-clean/apps/amp_uart1/src/main.c` 加共享内存 probe 写入。
3. 构建并出一个小热更新包。
4. 板上用 `devmem` 验证 `0x03b00000` counter。

不要在这一轮动 `0x03c00000` / `0x03d00000` / mailbox / RPMsg。
