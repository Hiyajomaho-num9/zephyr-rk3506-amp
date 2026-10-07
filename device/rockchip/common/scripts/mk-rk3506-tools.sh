#!/bin/bash -e
# SPDX-License-Identifier: MIT

usage_hook()
{
	usage_oneline "rk3506-tools" "build Linux RPMsg/CAN test tools"
}

BUILD_CMDS="rk3506-tools"
build_hook()
{
	local compiler= candidate
	for candidate in "$RK_SDK_DIR"/prebuilts/gcc/linux-x86/arm/*/bin/arm-none-linux-gnueabihf-gcc; do
		[ -x "$candidate" ] || continue
		compiler="$candidate"
		break
	done
	if [ -z "$compiler" ]; then
		fatal "ARM Linux compiler not found in SDK prebuilts"
		return 1
	fi
	make -C "$RK_SDK_DIR/tools/rk3506_rpmsg_char_ping" \
		CC="$compiler" O="$RK_OUTDIR/tools/rk3506_rpmsg_char_ping" -j"$(nproc)"
	finish_build rk3506-tools
}

source "${RK_BUILD_HELPER:-$(dirname "$(realpath "$0")")/../build-hooks/build-helper}"
build_hook "$@"
