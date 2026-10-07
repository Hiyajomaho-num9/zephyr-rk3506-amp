# RK3506 BSP metadata audit

> Historical metadata audit for the former layout. Current requirements and
> validation status are in [BSP-CHECKLIST.md](../../../../docs/rk3506/BSP-CHECKLIST.md).

Date: 2026-05-16

## Scope

This audit covers the metadata-only compliance pass for the out-of-tree
`zephyr-rk3506` module:

- board metadata
- SoC metadata and Kconfig selection hierarchy
- sample/test YAML
- sample documentation

Runtime RPMsg, mailbox IRQ, cache, and shared-memory behavior are unchanged by
this pass.

## Official-doc basis

| Area | Zephyr document | Applied requirement |
| --- | --- | --- |
| Board porting | `hardware/porting/board_porting.rst` | Board metadata stays under `boards/<vendor>/<board>`; `ok3506b_s12_amp_uart1` is the tested platform name and `rk3506` is the HWMv2 qualifier reported by `list_boards`. |
| SoC porting | `hardware/porting/soc_porting.rst` | `SOC_RK3506` now selects `SOC_SERIES_RK3506`; `SOC_SERIES` defaults to `rk3506`; Rockchip family selection follows upstream Rockchip SoC Kconfig style. |
| Twister metadata | `develop/test/twister.rst` | Hardware-dependent sample/test entries are build-only and declare `integration_platforms`. |
| Sample criteria | `samples/sample_definition_and_criteria.rst` | The production RPMsg sample now has `README.rst` and keeps validation workloads in tests. |

## Decisions

### Board vendor and commercial name

The physical board is Forlinx OK3506B-S12. The out-of-tree board directory is
therefore `boards/forlinx/ok3506b_s12/`.

The Zephyr build platform remains normalized as `ok3506b_s12_amp_uart1`,
because that name is used by CMake, Kconfig, DTS filenames, Twister metadata,
and SDK scripts. `list_boards` reports `rk3506` as the HWMv2 qualifier, but
this Zephyr v4.4 checkout's Twister platform metadata matches the unqualified
`Platform.name`.

`forlinx` is not present in the upstream Zephyr vendor-prefix list, so this
module carries a local `dts/bindings/vendor-prefixes.txt`. Zephyr's devicetree
docs explicitly allow custom vendor-prefix files in any `DTS_ROOT`.

### Board qualifier

`ok3506b_s12_amp_uart1` is kept as the canonical board platform for now.

Reason: the current firmware is not a generic RK3506 application. It is the
OK3506 AMP CPU2 image loaded by the Linux/U-Boot FIT flow at `0x03e00000`, with
UART1 console and Linux-owned RPMsg memory. Representing CPU2 as a Zephyr CPU
cluster remains an open design task, but changing it now would be a functional
BSP rework rather than metadata cleanup.

### SoC series and family

`SOC_SERIES_RK3506` was added and selected by `SOC_RK3506`. It selects the
existing upstream `SOC_FAMILY_ROCKCHIP` symbol, matching the upstream Rockchip
SoC Kconfig pattern.

The local `soc.yml` still describes only the local `rk3506` series and SoC. It
does not duplicate Zephyr's upstream Rockchip family metadata.

### Sample/test Twister policy

Both RK3506 AMP entries are `build_only`.

Reason: runtime success requires the OK3506 Linux AMP host, Rockchip mailbox
routing, the Linux RPMsg char device, and board-specific flashing. That is not
an ordinary automated Twister console harness. Board runtime validation remains
documented separately through the Phase 4/5 logs and cache-stress scripts.

## Deferred compliance work

- Decide whether mailbox/RPMsg glue should later graduate from documented
  SoC platform glue into a Zephyr MBOX driver or IPC service backend.
- Decide whether CPU2 should be represented as a CPU cluster in `soc.yml`
  instead of being encoded in the board purpose/name.

## Phase 6.6 update

Phase 6.6 closed the two metadata gaps left by the initial audit:

- public RK3506 feature Kconfig symbols now use `SOC_RK3506_*`;
- the mailbox/RPMsg boundary is explicitly documented as RK3506 SoC platform
  glue for this out-of-tree BSP.

The production sample was also split from validation sources. Probe and
cache-thrash files now live only under `tests/ztest/rk3506_amp_cache_stress/`.

The remaining upstream submission gaps are tracked in:

```text
RK3506_UPSTREAM_GAP_REPORT.md
```

## Build-system fix found during Twister validation

Twister builds with `-Werror`. That exposed an RPMsg-Lite portability issue:
`rpmsg_env_bm.c` locally defined `ISR_COUNT` even though the RK3506 platform
configuration already provides it.

Fix:

- `rpmsg_lite/rpmsg_lite/rpmsg_env_bm.c` now keeps its upstream default only
  under `#ifndef ISR_COUNT`.

This is not a runtime behavior change for RK3506. RK3506 continues using the
platform value from `include/platform/RK3506/rpmsg_config.h`.

## Validation

Commands:

```sh
/home/kuro/.codex/skills/zephyr-bsp-compliance/scripts/check_rk3506_zephyr_layout.sh zephyr-rk3506

ZEPHYR_BASE=/home/kuro/zephyr-work/zephyr \
ZEPHYR_EXTRA_MODULES=/home/kuro/rk3506/OK3506B-S12_Linux6.1.99/SDK/OK3506_Linux_Source/zephyr-rk3506 \
ZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
CROSS_COMPILE=/home/kuro/rk3506/OK3506B-S12_Linux6.1.99/SDK/OK3506_Linux_Source/prebuilts/gcc/linux-x86/arm/gcc-arm-none-eabi-10-2020-q4-major-x86_64-linux/bin/arm-none-eabi- \
/home/kuro/venv-zephyr-4.4/bin/python3 /home/kuro/zephyr-work/zephyr/scripts/twister \
  -T zephyr-rk3506/samples/subsys/ipc/rpmsg/rk3506_pingpong \
  -p ok3506b_s12_amp_uart1 --build-only --inline-logs \
  -A zephyr-rk3506/boards --force-toolchain \
  --outdir output/twister-phase7_2-sample

ZEPHYR_BASE=/home/kuro/zephyr-work/zephyr \
ZEPHYR_EXTRA_MODULES=/home/kuro/rk3506/OK3506B-S12_Linux6.1.99/SDK/OK3506_Linux_Source/zephyr-rk3506 \
ZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
CROSS_COMPILE=/home/kuro/rk3506/OK3506B-S12_Linux6.1.99/SDK/OK3506_Linux_Source/prebuilts/gcc/linux-x86/arm/gcc-arm-none-eabi-10-2020-q4-major-x86_64-linux/bin/arm-none-eabi- \
/home/kuro/venv-zephyr-4.4/bin/python3 /home/kuro/zephyr-work/zephyr/scripts/twister \
  -T zephyr-rk3506/tests/ztest/rk3506_amp_cache_stress \
  -p ok3506b_s12_amp_uart1 --build-only --inline-logs \
  -A zephyr-rk3506/boards --force-toolchain \
  --outdir output/twister-phase7_2-cache

Python3_EXECUTABLE=/home/kuro/venv-zephyr-4.4/bin/python3 ./build.sh amp
```

Results:

- layout check: pass
- list_boards lookup for `ok3506b_s12_amp_uart1`: pass; qualifier reports as `rk3506`
- list_hardware lookup for `rk3506`: pass
- Twister sample build-only: 1 built, 0 failed, 0 errored
- Twister cache-stress build-only: 1 built, 0 failed, 0 errored
- SDK AMP build: pass

Phase 6.6 validation repeated these checks after the board path, Kconfig
namespace, and sample/test source split cleanup. Result: pass.

Artifacts:

| Artifact | SHA256 |
| --- | --- |
| `output/zephyr-build/amp2/zephyr/zephyr.bin` | `8d42ee618abe46caf27d3b5b333af7482d09f97a2f1d2b2a5323bf636fee528a` |
| `zephyr-rk3506/out/zephyr.bin` | `8d42ee618abe46caf27d3b5b333af7482d09f97a2f1d2b2a5323bf636fee528a` |
| `output/firmware/amp.img` | `f322400e9c38a2ae084639d22cbd9f450ea872c28e7e07f5569a5d7ce16083ee` |
