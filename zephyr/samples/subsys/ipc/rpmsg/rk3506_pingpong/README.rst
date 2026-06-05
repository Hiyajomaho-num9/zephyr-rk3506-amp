.. zephyr:code-sample:: rk3506-amp-pingpong
   :name: RK3506 AMP RPMsg pingpong

   Run RK3506 CPU2 firmware that announces ``rpmsg-ap3-ch0`` and replies to
   Linux RPMsg char-device ping traffic.

Overview
********

This sample is the normal RK3506 AMP CPU2 firmware path for the Forlinx
OK3506B-S12 board. It boots Zephyr at ``0x03e00000``, enables the UART1
console, brings up the Rockchip AMP mailbox/RPMsg-Lite link, announces
``rpmsg-ap3-ch0`` and replies ``pong:<seq>`` to Linux RPMsg requests.  If
CAN is enabled in the image, Linux can also ask the firmware to transmit or
receive CAN0 frames through RPMsg commands.  The sample does not contain fixed
periodic CAN application policy.

Validation-only shared-memory probes, verbose heartbeat logs, and private
cache-thrash workloads are intentionally disabled in the sample default
configuration. Those checks live in ``tests/ztest/rk3506_amp_cache_stress``.

Building and Running
********************

Build through the OK3506 SDK AMP image flow:

.. code-block:: console

   /path/to/zephyr-rk3506-amp/linux/scripts/install-to-sdk.sh /path/to/OK3506_Linux_Source
   cd /path/to/OK3506_Linux_Source
   ./build.sh amp
   ./build.sh updateimg

After flashing the generated image and booting Linux, verify the RPMsg channel:

.. code-block:: console

   ls -l /sys/bus/rpmsg/devices
   /userdata/rk3506_rpmsg_char_ping

Expected result:

.. code-block:: console

   ctrl=/dev/rpmsg_ctrl0 endpoint=/dev/rpmsg0 name=rpmsg-ap3-ch0 dst=0x3003 tx="ping:1"
   rx len=6 data="pong:1"

CAN0 RPMsg-Controlled Validation
********************************

The sample enables the RK3506 CAN driver, but CAN transmission is controlled
from Linux over RPMsg:

.. code-block:: console

   /userdata/rk3506_rpmsg_can 0x1b 0101030301000000
   /userdata/rk3506_rpmsg_can rx 0x1b 0x7ff 10000
   /userdata/rk3506_rpmsg_can_tx 0x1b 0101030301000000 1000 1000

The first command sends one Classical CAN frame.  The second command waits up
to 10 seconds for a standard CAN frame matching ID ``0x1b``.  The third command
sends a bounded repeated waveform for analyzer or motor-controller testing.

.. code-block:: text

   ID:   0x1b
   DLC:  8
   DATA: 01 01 03 03 01 00 00 00

For this AMP image, CAN0 is owned by Zephyr.  Linux ``can0`` is expected to be
disabled in the matching kernel devicetree so Linux SocketCAN and Zephyr do
not touch the same controller concurrently.
