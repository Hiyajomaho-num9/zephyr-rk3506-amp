#!/bin/sh
# SPDX-License-Identifier: MIT

set -eu

if [ "$#" -ne 1 ]; then
	echo "usage: $0 <OK3506_Linux_Source SDK root>" >&2
	exit 2
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(realpath "$SCRIPT_DIR/../..")
LINUX_DIR="$REPO_ROOT/linux"
SDK_ROOT=$(realpath "$1")

if [ ! -x "$SDK_ROOT/build.sh" ] || [ ! -d "$SDK_ROOT/kernel-6.1" ]; then
	echo "not an OK3506 Linux SDK root: $SDK_ROOT" >&2
	exit 1
fi

echo "[RK3506][INSTALL] repo=$REPO_ROOT"
echo "[RK3506][INSTALL] sdk =$SDK_ROOT"

rsync -a "$LINUX_DIR/kernel-6.1/" "$SDK_ROOT/kernel-6.1/"
rsync -a "$LINUX_DIR/device/" "$SDK_ROOT/device/"
rsync -a "$LINUX_DIR/tools/" "$SDK_ROOT/tools/"

CFG="$SDK_ROOT/device/rockchip/.chips/ok3506/amp-zephyr.cfg"
ESCAPED_REPO=$(printf '%s\n' "$REPO_ROOT" | sed 's/[\/&]/\\&/g')
sed -i "s#@RK3506_AMP_REPO@#$ESCAPED_REPO#g" "$CFG"

USERDATA="$SDK_ROOT/device/rockchip/common/extra-parts/userdata/normal"
chmod 0755 "$USERDATA"/rk3506_*.sh

if [ -n "${CC:-}" ]; then
	TOOL_CC="$CC"
else
	TOOL_CC=$(find "$SDK_ROOT/prebuilts/gcc/linux-x86/arm" \
		-path '*/bin/arm-none-linux-gnueabihf-gcc' -print -quit)
fi

if [ ! -x "$TOOL_CC" ]; then
	echo "arm Linux userspace compiler not found; set CC=/path/to/gcc" >&2
	exit 1
fi

make -C "$SDK_ROOT/tools/rk3506_rpmsg_char_ping" CC="$TOOL_CC" clean all
install -m 0755 \
	"$SDK_ROOT/tools/rk3506_rpmsg_char_ping/rk3506_rpmsg_can" \
	"$SDK_ROOT/tools/rk3506_rpmsg_char_ping/rk3506_rpmsg_can_tx" \
	"$SDK_ROOT/tools/rk3506_rpmsg_char_ping/rk3506_rpmsg_char_ping" \
	"$SDK_ROOT/tools/rk3506_rpmsg_char_ping/rk3506_rpmsg_char_stress" \
	"$SDK_ROOT/tools/rk3506_rpmsg_char_ping/rk3506_socketcan_send" \
	"$USERDATA/"

echo "[RK3506][INSTALL] done"
echo "Next:"
echo "  cd $SDK_ROOT"
echo "  choose/load device/rockchip/.chips/ok3506/OK3506-S-MINI_amp_nand_zephyr_defconfig"
echo "  ./build.sh clean-amp && ./build.sh amp"
echo "  ./build.sh kernel && ./build.sh updateimg"
