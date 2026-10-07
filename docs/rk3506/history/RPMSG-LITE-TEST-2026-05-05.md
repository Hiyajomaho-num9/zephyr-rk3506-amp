# RK3506 clean Zephyr RPMsg-Lite test package - 2026-05-05

## Goal

This package is the first Zephyr-side RPMsg-Lite remote test for RK3506 CPU2.

It keeps the proven clean Zephyr UART1 heartbeat path, then adds:

- RPMsg-Lite remote instance at `0x03c00000`
- link id `0x02`
- endpoint address `0x3003`
- nameservice channel `rpmsg-ap3-ch0`
- mailbox polling path for Linux `rockchip-rpmsg`
- shared-memory probe counters at `0x03b00000`

This is a runtime validation package, not the final production AMP design.

## Expected serial output

After flashing `artifacts/amp.img` and booting:

```text
[RK3506][CLEAN-ZEPHYR] main reached
[RK3506][RPMSG] remote init: shmem=0x03c00000 link=0x2
...
```

If the Linux side sends the expected initial virtio kick, the Zephyr log should then show:

```text
[RK3506][RPMSG] link up
[RK3506][RPMSG] ns announce: rpmsg-ap3-ch0 addr=0x3003
```

Heartbeat lines include:

```text
mbox_rx=<linux notify count> mbox_tx=<remote notify count> rpmsg_rx=<payload count> rpmsg_tx=<echo count> fail=<send failures>
```

## Linux-side checks

Run these on the RK3506 Linux shell:

```sh
dmesg | grep -Ei "rpmsg|virtio|mailbox|rockchip-rpmsg"
ls -l /sys/bus/rpmsg/devices 2>/dev/null
ls -l /sys/bus/rpmsg/drivers 2>/dev/null
ls -l /sys/bus/virtio/devices 2>/dev/null
```

Expected progression:

1. Before Zephyr nameservice, Linux usually shows only:
   - `virtio0.rpmsg_ctrl.0.0`
   - `virtio0.rpmsg_ns.53.53`
2. After Zephyr nameservice succeeds, a device for `rpmsg-ap3-ch0` should appear.

The current kernel has:

```text
CONFIG_RPMSG_NS=y
CONFIG_RPMSG_VIRTIO=y
CONFIG_RPMSG_ROCKCHIP_MBOX=y
# CONFIG_RPMSG_CHAR is not set
# CONFIG_RPMSG_CTRL is not set
# CONFIG_RPMSG_ROCKCHIP_TEST is not set
```

So this package mainly validates RPMsg discovery first. Ping/pong payload testing needs a matching Linux rpmsg test driver/module next.

## Shared-memory debug counters

The existing CPU2-to-Linux probe is still active:

```sh
devmem 0x03b00000 32  # magic, expected 0x524B5A50
devmem 0x03b00004 32  # version, expected 1
devmem 0x03b00008 32  # heartbeat counter
devmem 0x03b00014 32  # packed status
devmem 0x03b0001c 32  # mailbox bad-magic count
```

`0x03b00014` packs:

```text
bits 31:24 rpmsg_rx_count
bits 23:16 rpmsg_tx_count
bits 15:8  mailbox_rx_count
bits 7:0   nameservice_sent
```

Interpretation:

- `mailbox_rx_count == 0`: Zephyr did not see Linux mailbox notifications.
- `mailbox_rx_count > 0` and `nameservice_sent == 0`: RPMsg-Lite link did not come up.
- `nameservice_sent == 1` but no new Linux rpmsg device: nameservice/vring path needs debug.
- `mailbox bad-magic count > 0`: mailbox direction or register interpretation is wrong.

## Artifact hashes

Generated from the current build:

```text
amp.img     5e5aa7058ee3ff68831c680b02e1921913369b5c8cdd448c9c9852476e035621
zephyr.bin  060caf762f3c494ade3d1563d1aff064b18d370b8dc685bad6ee7baf7dd5e5c7
zephyr.elf  14bfc0b80fac140cf9ee70554eaf10eb2ecbd4031594515bb0ecdc352944b488
```
