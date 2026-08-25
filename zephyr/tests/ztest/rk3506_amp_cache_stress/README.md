# RK3506 AMP cache/RPMsg stress test

Validation firmware for Phase 5 cache and RPMsg stability checks.

This target intentionally enables:

- shared-memory diagnostics probe
- private D-cache/I-cache
- RPMsg endpoint `rpmsg-ap3-ch0`
- private cache-thrash worker

It is not the default production image. Use it when validating the cache
policy and long-run RPMsg path.

Standalone build from this repository:

```sh
cd /path/to/zephyr-rk3506-amp/zephyr
cmake -GNinja \
  -S tests/ztest/rk3506_amp_cache_stress \
  -B build/rk3506-amp-cache-stress \
  -DBOARD=ok3506b_s12_amp_uart1 \
  -DZEPHYR_TOOLCHAIN_VARIANT=cross-compile \
  -DCROSS_COMPILE=/path/to/arm-none-eabi- \
  -DZEPHYR_MODULES="$PWD/external_modules/hal/cmsis;$PWD/external_modules/lib/picolibc"
cmake --build build/rk3506-amp-cache-stress
```
