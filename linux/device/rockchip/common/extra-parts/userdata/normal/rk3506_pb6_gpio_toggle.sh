#!/bin/sh
# SPDX-License-Identifier: MIT
#
# RK3506 GPIO0_PB6 pad verification helper.
# This temporarily switches GPIO0_PB6 away from RMIO/CAN0_TX to GPIO output,
# toggles it, then restores the CAN0 PB6/PC0 RMIO route on exit.

set -eu

GPIO0_DR_L=0xff940000
GPIO0_DDR_L=0xff940008
GPIO0_EXT_PORT=0xff940070

GPIO0B_IOMUX_SEL_1=0xff95000c
GPIO0C_IOMUX_SEL_0=0xff950010
GPIO0B_DS_3=0xff95011c
GPIO0B_PULL=0xff950204
GPIO0B_IE=0xff950304
GPIO0B_OD=0xff950704
GPIO0C_PULL=0xff950208
GPIO0C_IE=0xff950308

RM_GPIO0B6_SEL=0xff9100b8
RM_GPIO0C0_SEL=0xff9100c0

PB6_BIT=0x4000
PB6_IOC_BIT=0x40

read32()
{
	devmem "$1" 32
}

write32()
{
	devmem "$1" 32 "$2" >/dev/null
}

wm32()
{
	reg="$1"
	mask="$2"
	value="$3"
	write32 "$reg" "$(( (mask << 16) | (value & mask) ))"
}

saved_dr=$(read32 "$GPIO0_DR_L")
saved_ddr=$(read32 "$GPIO0_DDR_L")
saved_iomux_b=$(read32 "$GPIO0B_IOMUX_SEL_1")
saved_iomux_c=$(read32 "$GPIO0C_IOMUX_SEL_0")
saved_ds3_b=$(read32 "$GPIO0B_DS_3")
saved_pull_b=$(read32 "$GPIO0B_PULL")
saved_ie_b=$(read32 "$GPIO0B_IE")
saved_od_b=$(read32 "$GPIO0B_OD")
saved_pull_c=$(read32 "$GPIO0C_PULL")
saved_ie_c=$(read32 "$GPIO0C_IE")
saved_rm_pb6=$(read32 "$RM_GPIO0B6_SEL")
saved_rm_pc0=$(read32 "$RM_GPIO0C0_SEL")

restore()
{
	echo
	echo "[PB6-TEST] restoring saved GPIO/RMIO state"
	wm32 "$GPIO0_DR_L" 0xffff "$((saved_dr & 0xffff))"
	wm32 "$GPIO0_DDR_L" 0xffff "$((saved_ddr & 0xffff))"
	wm32 "$GPIO0B_OD" 0xff "$((saved_od_b & 0xff))"
	wm32 "$GPIO0B_DS_3" 0x3f "$((saved_ds3_b & 0x3f))"
	wm32 "$GPIO0B_PULL" 0x3000 "$((saved_pull_b & 0x3000))"
	wm32 "$GPIO0B_IE" 0x40 "$((saved_ie_b & 0x40))"
	wm32 "$GPIO0C_PULL" 0x3 "$((saved_pull_c & 0x3))"
	wm32 "$GPIO0C_IE" 0x1 "$((saved_ie_c & 0x1))"
	wm32 "$GPIO0B_IOMUX_SEL_1" 0xffff "$((saved_iomux_b & 0xffff))"
	wm32 "$GPIO0C_IOMUX_SEL_0" 0xffff "$((saved_iomux_c & 0xffff))"
	wm32 "$RM_GPIO0B6_SEL" 0x7f "$((saved_rm_pb6 & 0x7f))"
	wm32 "$RM_GPIO0C0_SEL" 0x7f "$((saved_rm_pc0 & 0x7f))"
	echo "[PB6-TEST] restore done"
}

trap restore INT TERM EXIT

echo "[PB6-TEST] saved:"
echo "  DR_L=$saved_dr DDR_L=$saved_ddr EXT=$(read32 "$GPIO0_EXT_PORT")"
echo "  GPIO0B_IOMUX_SEL_1=$saved_iomux_b GPIO0C_IOMUX_SEL_0=$saved_iomux_c"
echo "  GPIO0B_DS_3=$saved_ds3_b GPIO0B_PULL=$saved_pull_b GPIO0B_IE=$saved_ie_b GPIO0B_OD=$saved_od_b"
echo "  GPIO0C_PULL=$saved_pull_c GPIO0C_IE=$saved_ie_c"
echo "  RM_GPIO0B6_SEL=$saved_rm_pb6 RM_GPIO0C0_SEL=$saved_rm_pc0"

echo "[PB6-TEST] switch GPIO0_PB6 to GPIO output"
# GPIO0_PB6 mux bits are GPIO0B_IOMUX_SEL_1[11:8]. Write-mask high 16 bits.
wm32 "$GPIO0B_IOMUX_SEL_1" 0x0f00 0x0000
# Disable open-drain on PB6.
wm32 "$GPIO0B_OD" "$PB6_IOC_BIT" 0x0
# Set a non-weak drive strength for PB6 and disable pull. GPIO0B_DS_3[5:0] is PB6.
wm32 "$GPIO0B_DS_3" 0x3f 0x3f
wm32 "$GPIO0B_PULL" 0x3000 0x0000

# GPIO v2 SWPORT_DR_L/SWPORT_DDR_L are write-mask registers:
# lower 16 bits are value, upper 16 bits are write enable.
wm32 "$GPIO0_DDR_L" "$PB6_BIT" "$PB6_BIT"

echo "[PB6-TEST] toggling GPIO0_PB6 every 500ms. Ctrl-C to stop."
echo "[PB6-TEST] If your measured pad is correct, it must toggle."

while true; do
	wm32 "$GPIO0_DR_L" "$PB6_BIT" "$PB6_BIT"
	echo "PB6=1 DR=$(read32 "$GPIO0_DR_L") DDR=$(read32 "$GPIO0_DDR_L") EXT=$(read32 "$GPIO0_EXT_PORT")"
	sleep 0.5

	wm32 "$GPIO0_DR_L" "$PB6_BIT" 0x0
	echo "PB6=0 DR=$(read32 "$GPIO0_DR_L") DDR=$(read32 "$GPIO0_DDR_L") EXT=$(read32 "$GPIO0_EXT_PORT")"
	sleep 0.5
done
