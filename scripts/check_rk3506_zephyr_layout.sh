#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

sdk_root=$(cd "${1:-$(dirname "${BASH_SOURCE[0]}")/..}" && pwd -P)
root="$sdk_root/zephyr"

fail()
{
	printf '[FAIL] %s\n' "$*" >&2
	exit 1
}

need_file()
{
	[ -f "$root/$1" ] || fail "missing $root/$1"
}

need_dir()
{
	[ -d "$root/$1" ] || fail "missing $root/$1"
}

need_grep()
{
	local pattern="$1"
	local file="$2"

	grep -Eq "$pattern" "$root/$file" || fail "missing pattern '$pattern' in $root/$file"
}

[ -d "$root" ] && [ ! -L "$root" ] || fail "zephyr must be an SDK-local real directory"
[ "$(git -C "$sdk_root" rev-parse --show-toplevel)" = "$sdk_root" ] || fail "Git root must be SDK root"
[ ! -e "$sdk_root/zephyr-rk3506" ] || fail "legacy Zephyr directory is still active"
[ ! -L "$sdk_root/zephyr-rk3506-amp-link" ] || fail "external Zephyr link is still active"
need_file "CMakeLists.txt"
need_file "Kconfig"
need_file "VERSION"
need_file "external_modules/hal/cmsis/zephyr/module.yml"
need_file "external_modules/lib/picolibc/zephyr/module.yml"

RK_SDK_DIR="$sdk_root"
RK_OUTDIR="$sdk_root/output"
unset RK_AMP_ZEPHYR_APP RK_AMP_ZEPHYR_BUILD_DIR RK_AMP_ZEPHYR_BOARD
source "$sdk_root/device/rockchip/.chips/ok3506/amp-zephyr.cfg"
[ "$RK_AMP_ZEPHYR_DIR" = "$root" ] || fail "AMP source root is not SDK-local"
[ -z "$RK_AMP_ZEPHYR_PREBUILT_BIN" ] || fail "private prebuilt shortcut must stay disabled"
for path in "$RK_AMP_ZEPHYR_APP" "$RK_AMP_ZEPHYR_BOARD_ROOT" "$RK_AMP_ZEPHYR_SOC_ROOT" "$RK_AMP_ZEPHYR_DTS_ROOT"; do
	case "$(realpath -m -- "$path")" in "$root"|"$root"/*) ;; *) fail "external AMP input: $path" ;; esac
done
if grep -Eq 'zephyr-rk3506-amp-link|my_code/zephyr-rk3506-amp|@RK3506_AMP_REPO@' \
	"$sdk_root/device/rockchip/.chips/ok3506/amp-zephyr.cfg" \
	"$sdk_root/device/rockchip/.chips/ok3506/amp_linux_zephyr.its"; then
	fail "retired paths remain in active AMP configuration"
fi

need_dir "boards/forlinx/ok3506b_s12"
need_file "boards/forlinx/ok3506b_s12/board.yml"
need_file "boards/forlinx/ok3506b_s12/ok3506b_s12_amp_uart1.dts"
need_file "boards/forlinx/ok3506b_s12/ok3506b_s12_amp_uart1.yaml"
need_file "boards/forlinx/ok3506b_s12/ok3506b_s12_amp_uart1_defconfig"
need_grep 'vendor: forlinx' "boards/forlinx/ok3506b_s12/board.yml"
need_grep 'identifier: ok3506b_s12_amp_uart1' \
	"boards/forlinx/ok3506b_s12/ok3506b_s12_amp_uart1.yaml"
need_grep 'zephyr,canbus = &can0;' \
	"boards/forlinx/ok3506b_s12/ok3506b_s12_amp_uart1.dts"

need_dir "soc/rockchip/rk3506"
need_file "soc/rockchip/rk3506/Kconfig.soc"
need_file "soc/rockchip/rk3506/CMakeLists.txt"
need_file "dts/arm/rockchip/rk3506.dtsi"
need_file "dts/bindings/vendor-prefixes.txt"
need_grep '^forlinx[[:space:]]' "dts/bindings/vendor-prefixes.txt"

need_file "dts/bindings/can/rockchip,rk3506-canfd.yaml"
need_file "drivers/can/Kconfig"
need_file "drivers/can/CMakeLists.txt"
need_file "drivers/can/can_rk3506.c"
grep -qs 'config CAN_RK3506' "$root"/drivers/can/Kconfig* || fail "CAN_RK3506 Kconfig missing"
need_grep 'DEVICE_API\(can, rk3506_can_api\)' "drivers/can/can_rk3506.c"
need_grep '\.add_rx_filter = rk3506_can_add_rx_filter' "drivers/can/can_rk3506.c"
need_grep 'IRQ_CONNECT' "drivers/can/can_rk3506.c"

need_file "samples/subsys/ipc/rpmsg/rk3506_pingpong/sample.yaml"
need_file "samples/subsys/ipc/rpmsg/rk3506_pingpong/README.rst"
need_file "tests/drivers/can/rk3506_api/testcase.yaml"
need_file "tests/drivers/can/rk3506_api/src/main.c"

if grep -R "CONFIG_SAMPLE_RK3506_CAN_TX\\|SOC_RK3506_CAN_RPMSG_BRIDGE\\|SOC_RK3506_CAN0_RMIO14_16_PINMUX" \
	"$root/samples" "$root/tests" "$root/drivers" "$root/soc" >/dev/null 2>&1; then
	fail "obsolete CAN bring-up Kconfig symbol found"
fi

printf '[OK] RK3506 SDK-root layout checks passed for %s\n' "$sdk_root"
