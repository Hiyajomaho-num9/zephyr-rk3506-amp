#!/bin/sh
# SPDX-License-Identifier: MIT
#
# RK3506 GPIO0_PC0 pad verification helper.
# This temporarily switches GPIO0_PC0 away from RMIO/CAN0_RX to GPIO input,
# checks internal pull-up/down behaviour, then restores the CAN0 PB6/PC0
# RMIO route on exit.

set -eu

GPIO0_DDR_H=0xff94000c
GPIO0_EXT_PORT=0xff940070

GPIO0B_IOMUX_SEL_1=0xff95000c
GPIO0C_IOMUX_SEL_0=0xff950010
GPIO0C_PULL=0xff950208
GPIO0C_IE=0xff950308
GPIO0C_SMT=0xff950408

RM_GPIO0B6_SEL=0xff9100b8
RM_GPIO0C0_SEL=0xff9100c0

PC0_DDR_H_BIT=0x0001
PC0_EXT_BIT=0x00010000
PC0_IOC_BIT=0x0001

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

saved_ddr_h=$(read32 "$GPIO0_DDR_H")
saved_iomux_b=$(read32 "$GPIO0B_IOMUX_SEL_1")
saved_iomux_c=$(read32 "$GPIO0C_IOMUX_SEL_0")
saved_pull_c=$(read32 "$GPIO0C_PULL")
saved_ie_c=$(read32 "$GPIO0C_IE")
saved_smt_c=$(read32 "$GPIO0C_SMT")
saved_rm_pb6=$(read32 "$RM_GPIO0B6_SEL")
saved_rm_pc0=$(read32 "$RM_GPIO0C0_SEL")

restore()
{
	echo
	echo "[PC0-TEST] restoring saved GPIO/RMIO state"
	wm32 "$GPIO0_DDR_H" 0xffff "$((saved_ddr_h & 0xffff))"
	wm32 "$GPIO0C_PULL" 0x3 "$((saved_pull_c & 0x3))"
	wm32 "$GPIO0C_IE" 0x1 "$((saved_ie_c & 0x1))"
	wm32 "$GPIO0C_SMT" 0x1 "$((saved_smt_c & 0x1))"
	wm32 "$GPIO0B_IOMUX_SEL_1" 0xffff "$((saved_iomux_b & 0xffff))"
	wm32 "$GPIO0C_IOMUX_SEL_0" 0xffff "$((saved_iomux_c & 0xffff))"
	wm32 "$RM_GPIO0B6_SEL" 0x7f "$((saved_rm_pb6 & 0x7f))"
	wm32 "$RM_GPIO0C0_SEL" 0x7f "$((saved_rm_pc0 & 0x7f))"
	echo "[PC0-TEST] restore done"
}

trap restore INT TERM EXIT

print_sample()
{
	mode="$1"
	ext=$(read32 "$GPIO0_EXT_PORT")
	if [ $((ext & PC0_EXT_BIT)) -ne 0 ]; then
		level=1
	else
		level=0
	fi
	echo "PC0-$mode level=$level EXT=$ext DDR_H=$(read32 "$GPIO0_DDR_H") PULL_C=$(read32 "$GPIO0C_PULL") IE_C=$(read32 "$GPIO0C_IE") SMT_C=$(read32 "$GPIO0C_SMT")"
}

sample_mode()
{
	mode="$1"
	pull="$2"
	echo
	echo "[PC0-TEST] mode=$mode pull=$pull"
	wm32 "$GPIO0C_PULL" "$((PC0_IOC_BIT | (PC0_IOC_BIT << 1)))" "$pull"
	sleep 0.2
	i=0
	while [ "$i" -lt 8 ]; do
		print_sample "$mode"
		i=$((i + 1))
		sleep 0.25
	done
}

echo "[PC0-TEST] saved:"
echo "  DDR_H=$saved_ddr_h EXT=$(read32 "$GPIO0_EXT_PORT")"
echo "  GPIO0B_IOMUX_SEL_1=$saved_iomux_b GPIO0C_IOMUX_SEL_0=$saved_iomux_c"
echo "  GPIO0C_PULL=$saved_pull_c GPIO0C_IE=$saved_ie_c GPIO0C_SMT=$saved_smt_c"
echo "  RM_GPIO0B6_SEL=$saved_rm_pb6 RM_GPIO0C0_SEL=$saved_rm_pc0"

echo "[PC0-TEST] switch GPIO0_PC0 to GPIO input"
# GPIO0_PC0 mux bits are GPIO0C_IOMUX_SEL_0[3:0]. Write-mask high 16 bits.
wm32 "$GPIO0C_IOMUX_SEL_0" 0x000f 0x0000
# GPIO v2 SWPORT_DDR_H is write-mask; bit0 maps GPIO0_PC0.
wm32 "$GPIO0_DDR_H" "$PC0_DDR_H_BIT" 0x0
# Enable input and Schmitt trigger for PC0.
wm32 "$GPIO0C_IE" "$PC0_IOC_BIT" "$PC0_IOC_BIT"
wm32 "$GPIO0C_SMT" "$PC0_IOC_BIT" "$PC0_IOC_BIT"

echo "[PC0-TEST] Expectation:"
echo "  pull-up   -> level must become 1 if pad is not externally held low."
echo "  pull-down -> level must become 0 if pad is not externally held high."
echo "  If pull-up still reads 0, check wrong pad, short to GND, or external driver."

# RK3506 pull encoding from HAL: normal=0, up=1, down=2, keep=3.
sample_mode none 0x0
sample_mode pullup 0x1
sample_mode pulldown 0x2
sample_mode pullup 0x1

echo
echo "[PC0-TEST] holding pull-up. Ctrl-C to restore."
while true; do
	print_sample hold-up
	sleep 0.5
done
