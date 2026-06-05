# zephyr-rk3506-amp

RK3506 Linux + Zephyr AMP bring-up tree for Forlinx OK3506B-S12.

Layout:

- `zephyr/`: Zephyr source tree with RK3506 SoC, board, DTS, drivers, RPMsg sample, and CAN tests merged in.
- `linux/`: Linux SDK overlay: AMP DTS/config, FIT ITS, and Linux userspace test tools.

Build Zephyr directly:

```sh
cd zephyr
cmake -GNinja \
  -S samples/subsys/ipc/rpmsg/rk3506_pingpong \
  -B build/rk3506_pingpong \
  -DBOARD=ok3506b_s12_amp_uart1 \
  -DZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
  -DCROSS_COMPILE=/path/to/arm-none-eabi- \
  -DZEPHYR_MODULES="$PWD/external_modules/hal/cmsis;$PWD/external_modules/lib/picolibc" \
  -DBUILD_VERSION=rk3506-amp

cmake --build build/rk3506_pingpong
```

Install Linux-side files into the Forlinx SDK:

```sh
cd /path/to/zephyr-rk3506-amp/linux
./scripts/install-to-sdk.sh /path/to/OK3506_Linux_Source
```

Then build the vendor image from the SDK:

```sh
cd /path/to/OK3506_Linux_Source
# Select/load device/rockchip/.chips/ok3506/OK3506-S-MINI_amp_nand_zephyr_defconfig
./build.sh clean-amp
./build.sh amp
./build.sh kernel
./build.sh updateimg
```

Validated features:

- CPU2 Zephyr boot on RK3506 AMP load address `0x03e00000`.
- Linux `rockchip-rpmsg` channel `rpmsg-ap3-ch0`, Zephyr endpoint `0x3003`.
- RPMsg ping/pong and Linux userspace char-device tools.
- AMP shared memory/cache validation.
- RK3506 CAN0 Zephyr driver path controlled through RPMsg command tools.
