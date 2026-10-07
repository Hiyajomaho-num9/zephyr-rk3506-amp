# RK3506 Zephyr AMP/RPMsg bring-up notes

> Historical baseline report. Current image status and SDK-root commands are
> maintained in [STATUS.md](../../../docs/rk3506/STATUS.md) and
> [BUILD.md](../../../docs/rk3506/BUILD.md).

Date: 2026-05-15
Updated: 2026-05-16

## Status

Phase 5.6/5.7 is closed as of 2026-05-16.  The RK3506 Zephyr AMP foundation is
now validated through an 8-hour RPMsg/cache stress run using the hotfix1
package:

```text
phase5.6-5.7-zephyr-cache-stress-thrash-20260515-hotfix1
```

Validated baseline:

- U-Boot loads the AMP firmware as FIT loadable `amp2`.
- Zephyr runs on CPU2 at `0x03e00000`.
- UART1 console and Zephyr shell work.
- Linux creates the RPMsg device `rpmsg-ap3-ch0`.
- Linux userspace ping receives Zephyr pong.
- Linux-to-Zephyr notifications arrive through mailbox2 IRQ 176.
- CPU2 private I-cache and D-cache are enabled.
- AMP shared memory and RPMsg windows remain strongly ordered.
- RPMsg ping/pong remains stable while a Zephyr private D-cache thrash thread
  runs concurrently.

Phase 4.7 is the cleanup baseline used before Phase 5.
Phase 5.1 formalizes the RK3506 AMP memory layout in a shared Zephyr BSP
header without changing the verified RPMsg/cache behavior.
Phase 5.2 aligns Zephyr shared-memory MMU attributes with the RTT
`UNCACHED_MEM` baseline: `amp-shmem` and the full `0x03c00000/2M` RPMsg
window are mapped as strongly ordered, read/write, non-executable regions.
Phase 5.3 enables only the CPU2 private I-cache while keeping D-cache disabled
and shared memory strongly ordered. Phase 5.5 enables private D-cache under the
same shared-memory policy. Phase 5.6/5.7 validates long-run RPMsg/cache
stability.

## Memory layout

| Region | Address | Size | Use |
| --- | ---: | ---: | --- |
| amp-shmem / probe | `0x03b00000` | `0x00100000` | Phase 4/5 diagnostic status |
| RPMsg shared memory | `0x03c00000` | `0x00200000` | RTT-compatible Linux RPMsg shared window |
| RPMsg vring window | `0x03c00000` | `0x00020000` | Linux `rpmsg@3c00000` vring area |
| RPMsg vring0 | `0x03c00000` | `0x00008000` | Linux virtqueue 0 |
| RPMsg vring1 | `0x03c08000` | `0x00008000` | Linux virtqueue 1 |
| rpmsg-dma | `0x03d00000` | `0x00100000` | Linux reserved shared DMA pool |
| Zephyr image | `0x03e00000` | `0x00100000` | CPU2 firmware |

The canonical Zephyr definitions live in:

```text
zephyr-rk3506/soc/rockchip/rk3506/rk3506_amp_layout.h
```

These match the Linux reserved-memory layout used by the RK3506 AMP DTS and
the RT-Thread RK3506 defaults:

- Linux DTS reserves `0x03b00000/1M`, `0x03c00000/1M`,
  `0x03d00000/1M`, and `0x03e00000/1M`.
- Linux `rpmsg@3c00000` exposes only `0x03c00000/0x20000` as vring space.
- RTT builds `LINUX_RPMSG_BASE=0x03c00000`,
  `LINUX_RPMSG_SIZE=0x00200000`, and maps that window as `UNCACHED_MEM`.

Phase 5.2 changes Zephyr MMU attributes for the shared windows to an explicit
strongly-ordered policy. Phase 5.3 then enables only I-cache. D-cache remains
disabled; enabling D-cache or adding cache-maintenance policy is a later Phase 5
step.

## RPMsg parameters

| Item | Value |
| --- | --- |
| link id | `0x2` |
| channel name | `rpmsg-ap3-ch0` |
| Zephyr endpoint address | `0x3003` |
| Linux source address observed | `0x400` |
| test payload | `ping:x` -> `pong:x` |

Linux test command:

```sh
/userdata/rk3506_rpmsg_char_ping
```

Expected result:

```text
ctrl=/dev/rpmsg_ctrl0 endpoint=/dev/rpmsg0 name=rpmsg-ap3-ch0 dst=0x3003 tx="ping:1"
rx len=6 data="pong:1"
```

## Mailbox/GIC parameters

| Item | Value |
| --- | --- |
| mailbox instance | mailbox2 |
| Linux-to-Zephyr IRQ | `176` (`0xb0`) |
| GIC target byte | `0x4` |
| GIC group | Group0 |
| priority | `0xd0` |

The verified Zephyr path uses polling only during link-up and namespace
announce. After namespace announce, Linux-to-Zephyr notifications are handled
by the mailbox IRQ.

## RPMsg-Lite interrupt disable policy

RPMsg-Lite exposes two virtual queue interrupt IDs, but RK3506 uses one shared
physical mailbox IRQ for both queues. Therefore `platform_interrupt_disable()`
must not mask IRQ 176. It may record the virtual disable request for diagnostics,
but the shared physical mailbox IRQ must remain enabled.

This policy is required for stable Linux-to-Zephyr ping/pong after namespace
announce.

## Shared probe

`CONFIG_SOC_RK3506_AMP_PROBE=y` writes a diagnostic structure at `0x03b00000`.
Phase 4.7 uses probe version `7`. Phase 5.1 uses probe version `8`.
Phase 5.2 uses probe version `9`.
Phase 5.3 uses probe version `10`. Phase 5.4 uses probe version `11`.
Phase 5.5 uses probe version `13`. Phase 5.6/5.7 uses probe version `14`.

Useful fields:

| Offset | Meaning |
| ---: | --- |
| `0x00` | magic `0x524B5A50` |
| `0x04` | probe version |
| `0x14` | packed RPMsg status |
| `0x20` | mailbox IRQ count |
| `0x40` | mailbox IRQ number |
| `0x4c` | GIC enable bit |
| `0x70` | GIC group bit |
| `0xbc` | compiled feature flags |
| `0xd8` | shared MMU policy tag, `0x5354524F` (`STRO`) |
| `0xdc` | `amp-shmem` Zephyr MMU attrs |
| `0xe0` | `rpmsg` Zephyr MMU attrs |
| `0xe4` | SCTLR snapshot |
| `0xe8` | cache policy tag, `0x49434F4E` (`ICON`) |
| `0xec` | SCTLR.I bit |
| `0xf0` | SCTLR.C bit |
| `0xf4` | SCTLR.A bit |

Feature flags:

| Bit | Meaning |
| ---: | --- |
| 0 | mailbox IRQ enabled |
| 1 | GIC target routing enabled |
| 2 | GIC Group1 requested |
| 3 | GICv2 enabled |
| 4 | IRQ-only after namespace announce |
| 5 | periodic keep-enable enabled |

Phase 4.7 / Phase 5.1 expected flags:

```text
0x0000001B
```

## Phase 4.7 validation

```sh
ls -l /sys/bus/rpmsg/devices
/userdata/rk3506_rpmsg_char_ping

for a in 00 04 14 20 24 28 2c 40 4c 70 bc cc d0 d4; do
  printf "0x03b000$a = "
  devmem 0x03b000$a 32
done
```

Expected:

```text
0x03b00000 = 0x524B5A50
0x03b00004 = 0x00000008
0x03b00020 >= 0x00000001
0x03b00040 = 0x000000B0
0x03b0004c = 0x00000001
0x03b00070 = 0x00000000
0x03b000bc = 0x0000001B
```

Zephyr UART should print only the important bring-up messages by default:

```text
[RK3506][CLEAN-ZEPHYR] main reached
[RK3506][RPMSG] remote init: shmem=0x03c00000 link=0x2
[RK3506][RPMSG] link up
[RK3506][RPMSG] ns announce: rpmsg-ap3-ch0 addr=0x3003
```

Verbose heartbeat and per-packet logs are controlled by
`CONFIG_SOC_RK3506_RPMSG_DEBUG_LOG`.

## RTT comparison

The RT-Thread baseline also uses:

- CPU2 AMP firmware loaded at `0x03e00000`;
- RPMsg shared memory starting at `0x03c00000`;
- mailbox2 notification path;
- link id `0x2`;
- Linux-visible channel `rpmsg-ap3-ch0`.

Zephyr now reproduces this minimal communication path. Phase 5 will address the
remaining structural difference: RTT has dedicated uncached/shared-memory
handling, while Zephyr still needs formal MMU/cache/reserved-memory cleanup.

## Phase 5 entry

Phase 5 is:

```text
MMU/cache/reserved-memory formalization
```

Main goals:

- replace ad-hoc shared-memory assumptions with explicit Zephyr MMU regions;
- make RPMsg vring/buffer memory non-cacheable or otherwise coherently managed;
- align Zephyr linker, Linux reserved-memory, and U-Boot FIT load address;
- document the replacement for RTT's uncached heap / `.linux_share_rpmsg`.

## Phase 5.1 boundary

Phase 5.1 is intentionally low-risk:

- adds `rk3506_amp_layout.h` as the single source of truth for AMP/RPMsg
  memory constants;
- updates Zephyr users of `0x03b00000`, `0x03c00000`, link id, endpoint and
  channel name to use those constants;
- bumps the shared probe version to `8`;
- keeps current MMU attributes unchanged:
  `amp-shmem` and `rpmsg` are still `MT_NORMAL | MATTR_SHARED`;
- keeps the current reset hook behavior unchanged: I-cache and D-cache remain
  disabled.

Phase 5.2 should change the actual MMU attributes for `0x03b00000`,
`0x03c00000`, and `0x03d00000` to an explicit non-cacheable policy and then
re-run the same RPMsg validation.

## Phase 5.2 boundary

Phase 5.2:

- maps `0x03b00000/1M` and `0x03c00000/2M` as:
  `MT_STRONGLY_ORDERED | MPERM_R | MPERM_W | MATTR_MAY_MAP_L1_SECTION`;
- covers both the Linux vring window and `rpmsg-dma` because the RPMsg MMU
  region spans `0x03c00000-0x03dfffff`;
- keeps the verified RPMsg endpoint/channel behavior unchanged;
- keeps cache disabled in reset hook; this phase only fixes the formal MMU
  policy.

Expected Phase 5.2 probe:

```sh
ls -l /sys/bus/rpmsg/devices
/userdata/rk3506_rpmsg_char_ping

for a in 00 04 14 20 24 28 2c 40 4c 70 bc d8 dc e0 e4; do
  printf "0x03b000$a = "
  devmem 0x03b000$a 32
done
```

Expected key values:

```text
0x03b00000 = 0x524B5A50
0x03b00004 = 0x00000009
0x03b00020 >= 0x00000001
0x03b00040 = 0x000000B0
0x03b0004c = 0x00000001
0x03b00070 = 0x00000000
0x03b000bc = 0x0000001B
0x03b000d8 = 0x5354524F
0x03b000dc = 0x00010019
0x03b000e0 = 0x00010019
```

`0x00010019` means:

```text
MT_STRONGLY_ORDERED | MPERM_R | MPERM_W | MATTR_MAY_MAP_L1_SECTION
```

## Phase 5.3 boundary

Phase 5.3:

- enables `CONFIG_SOC_RK3506_PRIVATE_ICACHE=y`;
- sets SCTLR.I in `soc_reset_hook()`;
- still clears SCTLR.C and SCTLR.A;
- keeps shared AMP/RPMsg memory at `0x03b00000` and `0x03c00000/2M`
  strongly ordered;
- keeps the RPMsg endpoint/channel behavior unchanged.

Expected Phase 5.3 probe:

```sh
ls -l /sys/bus/rpmsg/devices
/userdata/rk3506_rpmsg_char_ping

for a in 00 04 14 20 24 28 2c 40 4c 70 bc d8 dc e0 e4 e8 ec f0 f4; do
  printf "0x03b000$a = "
  devmem 0x03b000$a 32
done
```

Expected key values:

```text
0x03b00000 = 0x524B5A50
0x03b00004 = 0x0000000A
0x03b00020 >= 0x00000001
0x03b00040 = 0x000000B0
0x03b0004c = 0x00000001
0x03b00070 = 0x00000000
0x03b000bc = 0x0000001B
0x03b000d8 = 0x5354524F
0x03b000dc = 0x00010019
0x03b000e0 = 0x00010019
0x03b000e8 = 0x49434F4E
0x03b000ec = 0x00000001
0x03b000f0 = 0x00000000
0x03b000f4 = 0x00000000
```

Phase 5.3 first package was rejected on board because Zephyr's generic
`z_arm_mmu_init()` re-enabled SCTLR.C after `soc_reset_hook()` had cleared it.
The observed probe was:

```text
0x03b00004 = 0x0000000A
0x03b000ec = 0x00000001
0x03b000f0 = 0x00000001
0x03b000f4 = 0x00000000
```

Phase 5.3.1 fixes that by:

- gating `ARM_MMU_SCTLR_DCACHE_ENABLE_BIT` in Zephyr's ARM MMU init when
  `CONFIG_SOC_RK3506_PRIVATE_ICACHE=y`;
- re-applying the RK3506 cache policy in both `soc_reset_hook()` and
  `soc_early_init_hook()`;
- bumping the probe version to `0x0000000B`.

Expected Phase 5.3.1 values are the same as Phase 5.3 except:

```text
0x03b00004 = 0x0000000B
0x03b000ec = 0x00000001
0x03b000f0 = 0x00000000
0x03b000f4 = 0x00000000
```

## Phase 5.4 boundary

Phase 5.4 is a D-cache preflight step. It does not enable D-cache yet.

It only adds runtime evidence for the cache/MMU boundary:

- shared AMP/RPMsg memory stays strongly ordered;
- I-cache stays enabled;
- D-cache and alignment-check stay disabled;
- MMU stays enabled;
- TTBR0/DACR/TTBCR snapshots are exported;
- Zephyr normal code/data/rodata intended MMU attrs are exported for review.

Use arithmetic address generation for offsets above `0xff`:

```sh
ls -l /sys/bus/rpmsg/devices
/userdata/rk3506_rpmsg_char_ping

for off in 0x00 0x04 0x14 0x20 0x24 0x28 0x2c 0x40 0x4c 0x70 \
           0xbc 0xd8 0xdc 0xe0 0xe4 0xe8 0xec 0xf0 0xf4 0xf8 \
           0xfc 0x100 0x104 0x108 0x10c 0x110 0x114 0x118; do
  addr=$((0x03b00000 + off))
  addr_hex=$(printf "0x%08x" "$addr")
  printf "%s = " "$addr_hex"
  devmem "$addr_hex" 32
done
```

Expected key values:

```text
0x03b00000 = 0x524B5A50
0x03b00004 = 0x0000000C
0x03b00020 >= 0x00000001
0x03b00024 >= 0x00000001
0x03b00028 = 0x00000002
0x03b0002c = 0x524D5347
0x03b00040 = 0x000000B0
0x03b0004c = 0x00000001
0x03b00070 = 0x00000000
0x03b000bc = 0x0000001B
0x03b000d8 = 0x5354524F
0x03b000dc = 0x00010019
0x03b000e0 = 0x00010019
0x03b000e8 = 0x49434F4E
0x03b000ec = 0x00000001
0x03b000f0 = 0x00000000
0x03b000f4 = 0x00000000
0x03b000f8 = 0x50464C54
0x03b000fc = 0x00000001
0x03b00104 = 0x55555555
0x03b00108 = 0x00000000
```

Field meanings:

```text
0x03b000f8 = ASCII "PFLT": D-cache preflight policy
0x03b000fc = SCTLR.M bit
0x03b00100 = TTBR0 snapshot, address varies by build
0x03b00104 = DACR snapshot
0x03b00108 = TTBCR snapshot
0x03b0010c = TTBR0 low attribute bits
0x03b00110 = Zephyr normal code attrs
0x03b00114 = Zephyr normal data attrs
0x03b00118 = Zephyr normal rodata attrs
```

## Phase 5.5 boundary

Phase 5.5 is the first controlled D-cache enablement package:

- enables `CONFIG_SOC_RK3506_PRIVATE_DCACHE=y`;
- keeps `0x03b00000/1M` and `0x03c00000/2M` strongly ordered;
- leaves alignment-check disabled;
- keeps the verified IRQ-only RPMsg path unchanged;
- bumps the probe version to `0x0000000D`.

The reset hook does not enable D-cache before MMU is on. D-cache is enabled
after `z_arm_mmu_init()` and then re-applied in `soc_early_init_hook()`.

Use the same arithmetic probe command as Phase 5.4. Expected key deltas:

```text
0x03b00004 = 0x0000000D
0x03b000e8 = 0x49444348
0x03b000ec = 0x00000001
0x03b000f0 = 0x00000001
0x03b000f4 = 0x00000000
0x03b000f8 = 0x44434143
0x03b000fc = 0x00000001
```

`0x49444348` is ASCII `IDCH`: I-cache + D-cache. `0x44434143` is
ASCII `DCAC`: D-cache validation package. RPMsg must still return `pong:1`.

## Phase 5.6/5.7 boundary

Phase 5.6 and Phase 5.7 are merged into one validation package.

It keeps the Phase 5.5 D-cache policy and adds two stress dimensions:

- Linux-side long-run RPMsg/cache pressure test;
- Zephyr-side private D-cache thrash thread running concurrently with RPMsg.

Firmware deltas:

- probe version is bumped to `0x0000000E`;
- RPMsg echo reply buffer is expanded so payload sizes up to 384 bytes are
  echoed exactly;
- a private Zephyr thread continuously mutates two 32 KiB aligned arrays to
  exercise D-cache-backed normal private memory while shared AMP/RPMsg memory
  remains strongly ordered.

Linux userdata tools:

- `/userdata/rk3506_rpmsg_char_stress`
- `/userdata/rk3506_cache_stress_8h.sh`

The stress tool keeps one `rpmsg_char` endpoint open for the whole run and
sends one outstanding message at a time.  It sweeps payload sizes:

```text
40, 63, 64, 65, 127, 128, 129, 255, 256, 384 bytes
```

Each request starts with `ping:` and each reply must be the exact same payload
with only the prefix changed to `pong:`.  The tool logs CRCs on mismatch and
hard-fails on timeout, short I/O, payload mismatch, or probe assertion failure.
At every progress interval it also verifies that the Zephyr private thrash
counter has advanced since the previous interval.

Probe hard assertions:

```text
0x03b00000 = 0x524B5A50
0x03b00004 = 0x0000000E
0x03b000dc = 0x00010019
0x03b000e0 = 0x00010019
0x03b000ec = 0x00000001
0x03b000f0 = 0x00000001
0x03b000f4 = 0x00000000
0x03b000f8 = 0x44434143
0x03b0011c = 0x54485253
0x03b00120 != 0x00000000
0x03b00128 = 0x00000000
```

`0x54485253` is ASCII `THRS`, meaning the private D-cache thrash thread is
active. This deliberately avoids a weak validation where RPMsg is stable only
because private cached memory is mostly idle.

Run the full 8-hour test:

```sh
/userdata/rk3506_cache_stress_8h.sh
```

Short smoke test:

```sh
/userdata/rk3506_cache_stress_8h.sh 300
```

Expected final result:

```text
RESULT: PASS tx=<N> rx=<N> ok=<N>
```

Failure criteria:

- any timeout;
- any mismatched reply;
- any write/read error;
- any probe assertion failure;
- probe `SCTLR.I` or `SCTLR.C` bit drops from `1`;
- probe `SCTLR.A` becomes `1`;
- probe shared-memory MMU attrs are not `0x00010019`;
- Zephyr private thrash counter is zero, stops advancing, or reports an error;
- Zephyr UART reports a fatal error or abort.

The wrapper writes logs under:

```text
/userdata/rk3506-cache-stress/
```

## Phase 5 closeout

Closeout date: 2026-05-16

Closed package:

```text
/home/kuro/rk3506/phase-archive/phase5.6-5.7-zephyr-cache-stress-thrash-20260515-hotfix1
```

Key artifact hashes:

```text
update.img                  7aabccc6554695bf6acf8fc057540389584ae65a20b517b7079857f15b0609ab
userdata.img                ce4b28c160610ccbe5a81f6d45aba70c0feb72d7e20ee22de5377c71a67dda83
rk3506_rpmsg_char_stress    937cdf36acf36756ae65d2b2c5fedd370c643fda7541e5b8cc745f12a9b7212a
```

Validation result:

- Board-side 8-hour RPMsg/cache stress completed.
- Probe version `0x0E` confirms the Phase 5.6/5.7 firmware path.
- Shared-memory attrs remained `0x00010019`.
- `SCTLR.I=1`, `SCTLR.C=1`, `SCTLR.A=0`.
- Zephyr private D-cache thrash policy `THRS` was active.
- RPMsg payload sweep stayed on the userspace `rpmsg_char` path.

Conclusion:

Phase 5 establishes the current RK3506 Zephyr AMP base:

- private I-cache/D-cache enabled on CPU2;
- Linux/Zephyr RPMsg over RK3506 mailbox IRQ is functional;
- shared AMP/RPMsg memory is intentionally non-cacheable/strongly ordered;
- long-run userspace RPMsg traffic remains compatible with concurrent private
  cached Zephyr workload.

Remaining work moves to formal BSP cleanup and production policy, not basic
AMP/RPMsg/cache bring-up.

## Phase 6.1 boundary

Phase 6.1 turns the Phase 5 validation firmware back into a cleaner BSP
default:

- keeps the verified RPMsg path:
  - mailbox2 BB IRQ `176`;
  - GIC target mask `0x4`;
  - Group0;
  - IRQ-only after namespace announce;
- keeps CPU2 private I-cache and D-cache enabled;
- keeps `0x03b00000/1M` and `0x03c00000/2M` mapped as strongly ordered;
- leaves the simple `rpmsg-ap3-ch0` ping/pong endpoint enabled;
- gates validation-only features behind Kconfig.

Default `zephyr-rk3506/samples/subsys/ipc/rpmsg/rk3506_pingpong/prj.conf` now disables:

```text
CONFIG_SOC_RK3506_AMP_DIAGNOSTICS
CONFIG_SOC_RK3506_AMP_PROBE
CONFIG_SOC_RK3506_CACHE_THRASH
CONFIG_SOC_RK3506_RPMSG_DEBUG_LOG
```

The normal package still prints one-shot boot/RPMsg status when
`CONFIG_SOC_RK3506_BOOT_BANNER=y`, but no longer writes the `0x03b00000` probe
structure or runs the private D-cache thrash thread by default.

Validation builds can re-enable the Phase 5 diagnostic path with:

```text
zephyr-rk3506/tests/ztest/rk3506_amp_cache_stress/prj.conf
```

The overlay enables:

```text
CONFIG_SOC_RK3506_AMP_DIAGNOSTICS=y
CONFIG_SOC_RK3506_AMP_PROBE=y
CONFIG_SOC_RK3506_CACHE_THRASH=y
```

Build verification performed for Phase 6.1:

- default AMP build via the vendor `./build.sh amp` path;
- stress overlay build via direct Zephyr CMake with
  `CONF_FILE="prj.conf;stress.conf"`;
- full `update.img` packaging via `./build.sh updateimg`.

Phase 6.1 artifact:

```text
/home/kuro/rk3506/phase-archive/phase6.1-zephyr-bsp-kconfig-cleanup-20260516
```
## Phase 6.2 tree layout normalization

Phase 6.2 moves the validated RK3506 Zephyr work from the temporary
staging layout into a Zephyr-style out-of-tree extension:

```text
zephyr-rk3506/
  zephyr/module.yml
  boards/forlinx/ok3506b_s12/
  soc/rockchip/rk3506/
  dts/arm/rockchip/rk3506.dtsi
  dts/bindings/mbox/rockchip,rk3506-mailbox.yaml
  dts/bindings/vendor-prefixes.txt
  samples/subsys/ipc/rpmsg/rk3506_pingpong/
  tests/ztest/rk3506_amp_cache_stress/
```

No functional RPMsg, GIC, MMU, or cache policy change is intended in this
phase. The SDK AMP ITS now builds the production sample from
`zephyr-rk3506/samples/subsys/ipc/rpmsg/rk3506_pingpong`, while the 8-hour
validation firmware is kept as `zephyr-rk3506/tests/ztest/rk3506_amp_cache_stress`.

After this phase, the old `zephyr/` and `zephyr-clean/` staging trees are no
longer active build inputs. They were moved to the phase archive as rollback
material so the SDK root has a single active Zephyr extension tree.

## Phase 6.3 documentation boundary

Phase 6.3 makes `zephyr-rk3506` the single documented entry point:

- `device/rockchip/.chips/ok3506/amp-zephyr.cfg` defaults now point at
  `zephyr-rk3506`;
- `zephyr-rk3506/README.md` contains the build and smoke-test commands;
- top-level `RK3506-ZEPHYR-AMP-QUICKSTART.md` is the operator-facing entry;
- historical documents are explicitly marked as historical when they mention
  old staging paths.

Phase 6.4 aligns the out-of-tree extension with Zephyr module and devicetree
layout rules:

- `zephyr/module.yml` declares `board_root`, `soc_root`, and `dts_root`;
- the RK3506 SoC include moved to `dts/arm/rockchip/rk3506.dtsi`;
- `rockchip,rk3506-mailbox` now has a minimal binding under
  `dts/bindings/mbox/`;
- sample/test CMake keeps `BOARD_ROOT`/`SOC_ROOT`/`DTS_ROOT` only as SDK
  direct-build compatibility glue.

## Phase 6.5 Forlinx OK3506B-S12 naming alignment

Phase 6.5 fixes the remaining board/sample/test naming mismatch:

- board metadata now lives under `boards/forlinx/ok3506b_s12/`;
- the Zephyr board platform is `ok3506b_s12_amp_uart1`; `list_boards` reports
  `rk3506` as the HWMv2 qualifier;
- the board DTS is `ok3506b_s12_amp_uart1.dts`;
- `forlinx` is declared as a local DTS vendor prefix in
  `dts/bindings/vendor-prefixes.txt`;
- the production RPMsg sample moved to
  `samples/subsys/ipc/rpmsg/rk3506_pingpong/`;
- the cache/RPMsg validation target moved to
  `tests/ztest/rk3506_amp_cache_stress/`.

This phase changes layout and metadata only. The validated RPMsg endpoint,
mailbox IRQ path, cache policy, and fixed AMP memory map are unchanged.

Current smoke test remains:

```sh
ls -l /sys/bus/rpmsg/devices
/userdata/rk3506_rpmsg_char_ping
```

Expected result:

```text
rx len=6 data="pong:1"
```

## Phase 6.6 upstream-compliance cleanup

Phase 6.6 is a structure and policy cleanup pass. It does not intentionally
change the validated RPMsg endpoint, mailbox IRQ path, MMU/cache policy, or
Linux ABI.

Changes:

- board directory is normalized to lower-case Zephyr style:
  `boards/forlinx/ok3506b_s12/`;
- public RK3506 feature Kconfig symbols use the SoC namespace
  `SOC_RK3506_*`;
- the production sample contains only the RPMsg ping/pong endpoint and one-shot
  boot/status messages;
- validation-only shared-memory probe and private cache-thrash sources remain
  only under `tests/ztest/rk3506_amp_cache_stress/`.

### RPMsg/mailbox architecture boundary

The current RK3506 RPMsg path is intentionally SoC platform glue, not a generic
Zephyr MBOX driver yet.

Reason:

- Linux owns the AMP host side through Rockchip `rockchip-rpmsg`;
- U-Boot loads the CPU2 firmware as FIT loadable `amp2` at `0x03e00000`;
- Linux DTS fixes `rpmsg@3c00000`, vring addresses, and the `rpmsg-dma`
  reserved pool;
- the CPU2 firmware is the RPMsg-Lite remote endpoint using mailbox2 IRQ 176 as
  the physical notification source;
- the verified runtime requires RK3506-specific GIC target routing
  (`0x4`, Group0) after Zephyr GIC init.

Therefore the current split is:

| Layer | Location | Responsibility |
| --- | --- | --- |
| SoC platform glue | `soc/rockchip/rk3506/rk3506_rpmsg_platform.c` | mailbox registers, GIC routing, RPMsg-Lite environment callbacks |
| SoC memory constants | `soc/rockchip/rk3506/rk3506_amp_layout.h` | Linux-compatible AMP/RPMsg addresses and channel parameters |
| Production sample | `samples/subsys/ipc/rpmsg/rk3506_pingpong/` | endpoint creation and `ping:` -> `pong:` policy |
| Validation target | `tests/ztest/rk3506_amp_cache_stress/` | probe, heartbeat, private cache-thrash and stress validation |

Upstream decision still required:

- keep this as documented RK3506 SoC platform glue;
- or factor mailbox routing into a Zephyr MBOX driver;
- or expose it through a Zephyr IPC-service backend once the Linux host
  memory/API boundary can be represented cleanly.
