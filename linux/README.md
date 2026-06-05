# RK3506 Linux AMP overlay

This directory contains the Linux-side pieces needed to build the RK3506
Linux + Zephyr AMP image in the Forlinx OK3506B-S12 Linux 6.1 SDK.

Contents:

- `kernel-6.1/arch/arm/boot/dts/`: AMP reserved-memory/RPMsg DTS files.
- `device/rockchip/.chips/ok3506/`: Zephyr AMP defconfig, FIT ITS, and AMP config.
- `tools/rk3506_rpmsg_char_ping/`: Linux userspace RPMsg/CAN test tools.
- `device/rockchip/common/extra-parts/userdata/normal/`: scripts installed to `/userdata`.

Install into an SDK:

```sh
./scripts/install-to-sdk.sh /path/to/OK3506_Linux_Source
```

Then build from the SDK root:

```sh
./build.sh clean-amp
./build.sh amp
./build.sh kernel
./build.sh updateimg
```
