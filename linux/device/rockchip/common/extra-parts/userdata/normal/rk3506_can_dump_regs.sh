#!/bin/sh
# RK3506 CAN0 golden-baseline register dump.

tag="${1:-can-dump}"

dump_addr()
{
	name="$1"
	addr="$2"
	printf "%-28s 0x%08x = " "$name" "$addr"
	devmem "$addr" 32
}

dump_can_off()
{
	off="$1"
	name="$2"
	addr=$((0xff320000 + 0x$off))
	printf "CAN0+0x%-4s %-18s 0x%08x = " "$off" "$name" "$addr"
	devmem "$addr" 32
}

echo "[RK3506][CAN-DUMP] tag=$tag"

if command -v ip >/dev/null 2>&1; then
	echo "[RK3506][CAN-DUMP] ip link:"
	ip -details -statistics link show can0 2>/dev/null || true
fi

echo "[RK3506][CAN-DUMP] CRU / pinmux:"
dump_addr "CRU_GATE_CON13" 0xff9a0834
dump_addr "CRU_CLKSEL_CON35" 0xff9a038c
dump_addr "CRU_CLKSEL_CON36" 0xff9a0390
dump_addr "CRU_SOFTRST_CON13" 0xff9a0a34
dump_addr "GPIO0B_IOMUX_SEL_1" 0xff95000c
dump_addr "GPIO0C_IOMUX_SEL_0" 0xff950010
dump_addr "RM_GPIO0B6_SEL" 0xff9100b8
dump_addr "RM_GPIO0C0_SEL" 0xff9100c0
dump_addr "GPIO0B_PULL" 0xff950028
dump_addr "GPIO0C_PULL" 0xff95002c
dump_addr "GPIO0B_IE" 0xff950068
dump_addr "GPIO0C_IE" 0xff95006c
dump_addr "GPIO0B_SMT" 0xff950088
dump_addr "GPIO0C_SMT" 0xff95008c
dump_addr "GPIO0B_OD" 0xff9500a8
dump_addr "GPIO0C_OD" 0xff9500ac

echo "[RK3506][CAN-DUMP] CAN0 registers:"
dump_can_off 0000 MODE
dump_can_off 0004 CMD
dump_can_off 0008 STATE
dump_can_off 000c INT
dump_can_off 0010 INT_MASK
dump_can_off 0100 NBTP
dump_can_off 0104 DBTP
dump_can_off 0108 TDCR
dump_can_off 010c BRS_CFG
dump_can_off 011c DMA_CTRL
dump_can_off 0200 TXFIC
dump_can_off 0204 TXID
dump_can_off 0208 TXDAT0
dump_can_off 020c TXDAT1
dump_can_off 0300 RXFIC
dump_can_off 0304 RXID
dump_can_off 030c RXDAT0
dump_can_off 0310 RXDAT1
dump_can_off 0400 RXFRD
dump_can_off 0600 STR_CTL
dump_can_off 0604 STR_STATE
dump_can_off 060c STR_WTM
dump_can_off 0700 ATF0
dump_can_off 0714 ATFM0
dump_can_off 0728 ATF_DLC
dump_can_off 072c ATF_CTL
dump_can_off 0808 AUTO_RETX_CFG
dump_can_off 0818 RXINT_CTRL
dump_can_off 081c RXINT_TIMEOUT
dump_can_off 0830 BUSOFFRCY_CFG
dump_can_off 0834 BUSOFF_RCY_THR
dump_can_off 0900 ERROR_CODE
dump_can_off 0904 ERROR_MASK
dump_can_off 0910 RXERRORCNT
dump_can_off 0914 TXERRORCNT
dump_can_off 0f0c RTL_VERSION
