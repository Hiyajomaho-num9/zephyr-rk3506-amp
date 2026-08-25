# RK3506 AMP RPMsg pingpong sample

Production-oriented CPU2 firmware for Forlinx OK3506B-S12 AMP.

Default behavior:

- boots Zephyr on CPU2 at `0x03e00000`
- brings up UART1 console
- announces RPMsg endpoint `rpmsg-ap3-ch0`
- replies `pong:<seq>` to Linux char-device ping traffic
- keeps validation-only shared-memory probe and cache-thrash code disabled

Build through the SDK AMP image flow:

```sh
./linux/scripts/install-to-sdk.sh /path/to/OK3506_Linux_Source
cd /path/to/OK3506_Linux_Source
./build.sh amp
./build.sh updateimg
```
