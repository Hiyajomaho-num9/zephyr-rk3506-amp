# RK3506 Zephyr AMP Quickstart

This is the current operator entry point for the OK3506 RK3506 Zephyr AMP
firmware.

## Active tree

```text
OK3506_Linux_Source/
  zephyr-rk3506/
    boards/rockchip/ok3506_amp_uart1/
    soc/rockchip/rk3506/
    samples/rk3506/amp_pingpong/
    tests/rk3506/amp_cache_stress/
    docs/
```

The upstream Zephyr checkout is external:

```text
/home/kuro/zephyr-work/zephyr
```

The SDK root should not contain active `zephyr/` or `zephyr-clean/` staging
trees.

## Build production update image

Run from the SDK root:

```sh
cd /home/kuro/rk3506/OK3506B-S12_Linux6.1.99/SDK/OK3506_Linux_Source
Python3_EXECUTABLE=/home/kuro/venv-zephyr-4.4/bin/python3 ./build.sh amp
Python3_EXECUTABLE=/home/kuro/venv-zephyr-4.4/bin/python3 ./build.sh updateimg
sha256sum output/update/Image/update.img output/firmware/amp.img zephyr-rk3506/out/zephyr.bin
```

Current production path:

```text
device/rockchip/.chips/ok3506/amp_linux_zephyr.its
  zephyr_app          = "zephyr-rk3506/samples/rk3506/amp_pingpong"
  zephyr_board        = "ok3506_amp_uart1"
  zephyr_board_root   = "zephyr-rk3506"
  zephyr_soc_root     = "zephyr-rk3506"
  zephyr_prebuilt_bin = "zephyr-rk3506/out/zephyr.bin"
```

## Flash artifact

Use:

```text
output/update/Image/update.img
```

The package contains the `amp` partition image built from Zephyr CPU2 firmware.

## Board smoke test

After flashing and booting Linux:

```sh
ls -l /sys/bus/rpmsg/devices
/userdata/rk3506_rpmsg_char_ping
```

Expected RPMsg devices:

```text
virtio0.rpmsg-ap3-ch0.-1.12291
virtio0.rpmsg_ctrl.0.0
virtio0.rpmsg_ns.53.53
```

Expected ping result:

```text
ctrl=/dev/rpmsg_ctrl0 endpoint=/dev/rpmsg0 name=rpmsg-ap3-ch0 dst=0x3003 tx="ping:1"
rx len=6 data="pong:1"
```

`platform mtd_vendor_storage: deferred probe pending` in Linux logs is unrelated
to this RPMsg smoke test.

## Validation firmware

The default production sample disables validation-only probe and cache-thrash
code.  For cache/RPMsg validation, build:

```sh
export ZEPHYR_BASE=/home/kuro/zephyr-work/zephyr
cmake -GNinja \
  -S zephyr-rk3506/tests/rk3506/amp_cache_stress \
  -B output/zephyr-build/rk3506-amp-cache-stress \
  -DBOARD=ok3506_amp_uart1 \
  -DBOARD_ROOT=$PWD/zephyr-rk3506 \
  -DSOC_ROOT=$PWD/zephyr-rk3506 \
  -DZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
  -DCROSS_COMPILE=$PWD/prebuilts/gcc/linux-x86/arm/gcc-arm-none-eabi-10-2020-q4-major-x86_64-linux/bin/arm-none-eabi- \
  -DPython3_EXECUTABLE=/home/kuro/venv-zephyr-4.4/bin/python3
cmake --build output/zephyr-build/rk3506-amp-cache-stress -j"$(nproc)"
```

The historical 8-hour validation flow used:

```sh
/userdata/rk3506_cache_stress_8h.sh
```

Expected final result:

```text
RESULT: PASS
```

## Current validated baseline

- CPU2 firmware load address: `0x03e00000`
- AMP diagnostics/shmem base: `0x03b00000`
- Linux RPMsg vrings: `0x03c00000`, `0x03c08000`
- RPMsg DMA/shared buffer pool: `0x03d00000`
- Zephyr RPMsg endpoint: `rpmsg-ap3-ch0`, address `0x3003`
- Linux userspace test tool: `/userdata/rk3506_rpmsg_char_ping`
- CPU2 private I-cache and D-cache are enabled
- AMP/RPMsg shared windows are mapped strongly ordered/non-cacheable
