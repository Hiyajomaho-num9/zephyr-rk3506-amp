#!/bin/bash -e

RK_RTOS_BSP_DIR=$RK_SDK_DIR/rtos/bsp/rockchip
ITS_FILE="$RK_CHIP_DIR/$RK_AMP_FIT_ITS"

RK_SCRIPTS_DIR="${RK_SCRIPTS_DIR:-$(dirname "$(realpath "$0")")}"

usage_hook()
{
	usage_oneline "amp" "build and pack amp system"
}

amp_get_value()
{
	echo "$1" | grep -owP "$2\s*=\s*<([^>]+)>" | awk -F'<|>' '{print $2}'
}

amp_get_string()
{
	echo "$1" | grep -owP "$2\s*=.*\"([^>]+)\"" | awk -F'"' '{print $2}'
}

amp_get_node()
{
	echo "$1" | \
	awk -v node="$2" \
		'$0 ~ node " {" {
		in_block = 1;
		block = $0;
		next;
		}
		in_block {
			block = block "\n" $0;
			if (/}/) {
				count_open = gsub(/{/, "&", block);
				count_close = gsub(/}/, "&", block);
				if (count_open == count_close) {
					in_block = 0;
					print block;
					block = "";
				}
			}
		}'
}

amp_touch_export()
{
	if [ -n "$2" ]; then
		DST=$2
	else
		DST=$1
	fi

	[ -n "${!1}" ] && export "$DST"="${!1}" || true
}

amp_resolve_path()
{
	# CMake must see one spelling of the source tree. Mixing the SDK symlink
	# with real app/SoC paths can load Zephyr's CMake wrappers recursively.
	# -m also supports build directories and prebuilt files not created yet.
	case "$1" in
		/*) realpath -m -- "$1";;
		*) realpath -m -- "$RK_SDK_DIR/$1";;
	esac
}

build_hal()
{
	local append=

	check_config "$1" || return 0

	message "=========================================="
	message "  Building CPU $2: HAL-->${!1}"
	message "=========================================="

	cd "$RK_RTOS_BSP_DIR/common/hal/project/"${!1}"/GCC"

	[ ! -n "$CC" ] || append=$CC
	(
		amp_touch_export FIRMWARE_CPU_BASE
		amp_touch_export DRAM_SIZE
		amp_touch_export SRAM_BASE
		amp_touch_export SRAM_SIZE
		amp_touch_export CUR_CPU

		make clean > /dev/null
		rm -rf $3.elf $3.bin
		make $append -j$(nproc) > ${RK_SDK_DIR}/hal.log 2>&1
	)

	cp TestDemo.elf $3.elf
	mv TestDemo.bin $3.bin
	ln -rsf $3.bin $RK_OUTDIR/$3.bin

	finish_build build_hal $@
}

build_rtthread()
{
	local append=

	check_config "$1" || return 0

	message "=========================================="
	message "  Building CPU $2: RT-Thread-->${!1}"
	message "                  Config-->$4"
	message "=========================================="

	cd "$RK_RTOS_BSP_DIR/${!1}"

	export RTT_ROOT=$RK_RTOS_BSP_DIR/../../

	amp_touch_export FIRMWARE_CPU_BASE RTT_PRMEM_BASE
	amp_touch_export DRAM_SIZE RTT_PRMEM_SIZE
	amp_touch_export SRAM_BASE RTT_SRAM_BASE
	amp_touch_export SRAM_SIZE RTT_SRAM_SIZE
	amp_touch_export SHMEM_BASE RTT_SHMEM_BASE
	amp_touch_export SHMEM_SIZE RTT_SHMEM_SIZE
	amp_touch_export CC RTT_EXEC_PATH

	ROOT_PART_OFFSET=$(rk_partition_start root)
	ROOT_PART_SIZE=$(rk_partition_size root)

	if [ -f "$4" ] ;then
		scons --useconfig="$4"
	else
		warning "Warning: Config $4 not exit!\n"
		warning "Default config(.config) will be used!\n"
	fi

	scons -c > /dev/null
	rm -rf gcc_arm.ld Image/rtt$2.elf Image/rtt$2.bin
	scons -j$(nproc) > ${RK_SDK_DIR}/rtt.log 2>&1

	cp rtthread.elf Image/rtt$2.elf
	mv rtthread.bin Image/rtt$2.bin
	ln -rsf Image/rtt$2.bin $RK_OUTDIR/$3.bin

	if [ -n "$RK_AMP_RTT_ROOT_DATA" ] && [ -n "$ROOT_PART_SIZE" ] ;then

		RTT_ROOT_USERDAT=$RK_RTOS_BSP_DIR/$RK_AMP_RTT_TARGET/$RK_AMP_RTT_ROOT_DATA

		ROOT_SECTOR_SIZE=$(grep -r "CONFIG_RT_DFS_ELM_MAX_SECTOR_SIZE" "$4" | cut -d '=' -f 2)
		if [ -z $ROOT_SECTOR_SIZE ];then
			ROOT_SECTOR_SIZE=4096
		fi

		./mkroot.sh root $RTT_ROOT_USERDAT $RK_CHIP_DIR/$RK_PARAMETER $ROOT_SECTOR_SIZE $RK_FIRMWARE_DIR/root.img
	fi

	finish_build build_rtthread $@
}

build_zephyr()
{
	local cpu="$1"
	local bin_name="$2"
	local zephyr_app="$3"
	local zephyr_board="$4"
	local zephyr_board_root="$5"
	local zephyr_dir="$6"
	local zephyr_prebuilt_bin="$7"
	local zephyr_conf="$8"
	local zephyr_soc_root="$9"
	local zephyr_dts_root="${10:-}"
	local zephyr_build_dir log_file
	local -a cmake_args
	local prebuilt_ok=0

	message "=========================================="
	message "  Building CPU $cpu: Zephyr"
	message "                  Board-->${zephyr_board:-${RK_AMP_ZEPHYR_BOARD:-unset}}"
	message "=========================================="

	amp_touch_export FIRMWARE_CPU_BASE
	amp_touch_export DRAM_SIZE
	amp_touch_export SRAM_BASE
	amp_touch_export SRAM_SIZE
	amp_touch_export SHMEM_BASE
	amp_touch_export SHMEM_SIZE
	amp_touch_export CUR_CPU

	[ -n "$zephyr_app" ] || zephyr_app="${RK_AMP_ZEPHYR_APP:-}"
	[ -n "$zephyr_board" ] || zephyr_board="${RK_AMP_ZEPHYR_BOARD:-}"
	[ -n "$zephyr_board_root" ] || zephyr_board_root="${RK_AMP_ZEPHYR_BOARD_ROOT:-}"
	[ -n "$zephyr_dir" ] || zephyr_dir="${RK_AMP_ZEPHYR_DIR:-${ZEPHYR_BASE:-}}"
	[ -n "$zephyr_prebuilt_bin" ] || zephyr_prebuilt_bin="${RK_AMP_ZEPHYR_PREBUILT_BIN:-}"
	[ -n "$zephyr_conf" ] || zephyr_conf="${RK_AMP_ZEPHYR_CONF:-}"
	[ -n "$zephyr_soc_root" ] || zephyr_soc_root="${RK_AMP_ZEPHYR_SOC_ROOT:-}"
	[ -n "$zephyr_dts_root" ] || zephyr_dts_root="${RK_AMP_ZEPHYR_DTS_ROOT:-}"
	zephyr_build_dir="${RK_AMP_ZEPHYR_BUILD_DIR:-$RK_OUTDIR/zephyr-build/$bin_name}"
	log_file="${RK_OUTDIR}/zephyr-build.log"

	[ -z "$zephyr_app" ] || zephyr_app="$(amp_resolve_path "$zephyr_app")"
	[ -z "$zephyr_board_root" ] || zephyr_board_root="$(amp_resolve_path "$zephyr_board_root")"
	[ -z "$zephyr_dir" ] || zephyr_dir="$(amp_resolve_path "$zephyr_dir")"
	[ -z "$zephyr_prebuilt_bin" ] || zephyr_prebuilt_bin="$(amp_resolve_path "$zephyr_prebuilt_bin")"
	[ -z "$zephyr_conf" ] || zephyr_conf="$(amp_resolve_path "$zephyr_conf")"
	[ -z "$zephyr_soc_root" ] || zephyr_soc_root="$(amp_resolve_path "$zephyr_soc_root")"
	[ -z "$zephyr_dts_root" ] || zephyr_dts_root="$(amp_resolve_path "$zephyr_dts_root")"
	[ -z "$zephyr_build_dir" ] || zephyr_build_dir="$(amp_resolve_path "$zephyr_build_dir")"

	# Only use prebuilt if it is newer than ALL local Zephyr source
	if [ -n "$zephyr_prebuilt_bin" ] && [ -f "$zephyr_prebuilt_bin" ]; then
		prebuilt_ok=1
		[ -d "$zephyr_app" ] && \
			[ "$(find "$zephyr_app" -type f -newer "$zephyr_prebuilt_bin" 2>/dev/null)" ] \
			&& prebuilt_ok=0
		[ -n "$zephyr_board_root" ] && [ -d "$zephyr_board_root/boards" ] && \
			[ "$(find "$zephyr_board_root/boards" -type f -newer "$zephyr_prebuilt_bin" 2>/dev/null)" ] \
			&& prebuilt_ok=0
		[ -n "$zephyr_soc_root" ] && [ -d "$zephyr_soc_root/soc" ] && \
			[ "$(find "$zephyr_soc_root/soc" -type f -newer "$zephyr_prebuilt_bin" 2>/dev/null)" ] \
			&& prebuilt_ok=0
		[ -n "$zephyr_dts_root" ] && [ -d "$zephyr_dts_root/dts" ] && \
			[ "$(find "$zephyr_dts_root/dts" -type f -newer "$zephyr_prebuilt_bin" 2>/dev/null)" ] \
			&& prebuilt_ok=0
		if [ "$prebuilt_ok" = "1" ]; then
			message "  Using prebuilt Zephyr binary (sources unchanged)"
			cp "$zephyr_prebuilt_bin" "$RK_OUTDIR/$bin_name.bin"
			finish_build build_zephyr $@
			return 0
		fi
		message "  Zephyr sources changed, rebuilding..."
	fi

	if [ -z "$zephyr_dir" ]; then
		fatal "No Zephyr base configured. Set RK_AMP_ZEPHYR_DIR or ZEPHYR_BASE, or provide RK_AMP_ZEPHYR_PREBUILT_BIN."
		return 1
	fi
	if [ ! -d "$zephyr_dir" ]; then
		fatal "Zephyr base not found: $zephyr_dir"
		return 1
	fi
	if [ -z "$zephyr_app" ]; then
		fatal "No Zephyr app configured. Set zephyr_app in ITS or RK_AMP_ZEPHYR_APP in $RK_CHIP_DIR/$RK_AMP_CFG."
		return 1
	fi
	if [ ! -d "$zephyr_app" ]; then
		fatal "Zephyr app not found: $zephyr_app"
		return 1
	fi
	if [ -z "$zephyr_board" ]; then
		fatal "No Zephyr board configured. Set zephyr_board in ITS or RK_AMP_ZEPHYR_BOARD in $RK_CHIP_DIR/$RK_AMP_CFG."
		return 1
	fi

	rm -rf "$zephyr_build_dir"
	mkdir -p "$zephyr_build_dir"
	: > "$log_file"

	# Find a usable ARM GNU toolchain.
	# Zephyr requires either the Zephyr SDK or the cross-compile variant.
	local tc_prefix tc_dir
	tc_dir="$RK_SDK_DIR/prebuilts/gcc/linux-x86/arm"
	for tc in "$tc_dir/"*"/bin"; do
		if [ -x "$tc/arm-none-eabi-gcc" ]; then
			tc_prefix="$tc/arm-none-eabi-"
			break
		fi
	done
	for tc in "$tc_dir/"*"/bin" "$tc_dir/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf/bin"; do
		[ -n "$tc_prefix" ] && break
		if [ -x "$tc/arm-none-linux-gnueabihf-gcc" ]; then
			tc_prefix="$tc/arm-none-linux-gnueabihf-"
			break
		fi
	done
	[ -z "$tc_prefix" ] && tc_prefix="${CROSS_COMPILE:-}"

	cmake_args=(
		-GNinja
		-S "$zephyr_app"
		-B "$zephyr_build_dir"
		-DBOARD="$zephyr_board"
		-DZEPHYR_TOOLCHAIN_VARIANT=cross-compile
		-DCROSS_COMPILE="$tc_prefix"
	)
	[ -n "$Python3_EXECUTABLE" ] && cmake_args+=("-DPython3_EXECUTABLE=$Python3_EXECUTABLE")
	[ -n "$zephyr_board_root" ] && cmake_args+=("-DBOARD_ROOT=$zephyr_board_root")
	[ -n "$zephyr_soc_root" ] && cmake_args+=("-DSOC_ROOT=$zephyr_soc_root")
	[ -n "$zephyr_dts_root" ] && cmake_args+=("-DDTS_ROOT=$zephyr_dts_root")
	[ -n "$zephyr_conf" ] && cmake_args+=("-DCONF_FILE=$zephyr_conf")
	# Explicit modules keep this SDK independent of external west workspaces.
	if [ -n "${RK_AMP_ZEPHYR_MODULES:-}" ]; then
		cmake_args+=("-DZEPHYR_MODULES=$RK_AMP_ZEPHYR_MODULES")
	fi
	# Additional modules remain available to other SDK AMP configurations.
	if [ -n "${RK_AMP_ZEPHYR_EXTRA_MODULES:-}" ]; then
		cmake_args+=("-DZEPHYR_EXTRA_MODULES=$RK_AMP_ZEPHYR_EXTRA_MODULES")
	fi
	# Optional extra conf fragment(s) (semicolon-separated).  EXTRA_CONF_FILE
	# overlays the app prj.conf; plain CONF_FILE replaces it, which silently
	# drops sample defaults (e.g. CONFIG_SHELL) — prefer EXTRA_CONF_FILE.
	if [ -n "${RK_AMP_ZEPHYR_EXTRA_CONF_FILE:-}" ]; then
		cmake_args+=("-DEXTRA_CONF_FILE=$RK_AMP_ZEPHYR_EXTRA_CONF_FILE")
	fi
	if [ -n "${RK_AMP_ZEPHYR_EXTRA_DTC_OVERLAY_FILE:-}" ]; then
		cmake_args+=("-DEXTRA_DTC_OVERLAY_FILE=$RK_AMP_ZEPHYR_EXTRA_DTC_OVERLAY_FILE")
	fi

	if [ -z "$tc_prefix" ]; then
		fatal "No ARM toolchain found in $tc_dir"
		return 1
	fi

	(
		export ZEPHYR_BASE="$zephyr_dir"
		if [ -n "${RK_AMP_ZEPHYR_MODULES:-}" ]; then
			unset EXTRA_ZEPHYR_MODULES ZEPHYR_EXTRA_MODULES
		fi
		cmake "${cmake_args[@]}" >> "$log_file" 2>&1
		cmake --build "$zephyr_build_dir" -j"$(nproc)" >> "$log_file" 2>&1
	)

	if [ ! -f "$zephyr_build_dir/zephyr/zephyr.bin" ]; then
		fatal "Zephyr binary not found: $zephyr_build_dir/zephyr/zephyr.bin"
		return 1
	fi

	cp "$zephyr_build_dir/zephyr/zephyr.bin" "$RK_OUTDIR/$bin_name.bin"

	# Refresh the prebuilt cache so future builds can fast-path
	if [ -n "$zephyr_prebuilt_bin" ]; then
		mkdir -p "$(dirname "$zephyr_prebuilt_bin")"
		cp "$zephyr_build_dir/zephyr/zephyr.bin" "$zephyr_prebuilt_bin"
		message "  Prebuilt binary updated: $zephyr_prebuilt_bin"
	fi

	finish_build build_zephyr $@
}

clean_hook()
{
	local rtt_dir hal_dir zephyr_build_dir

	[ "$RK_AMP" ] || return 0

	if [ -f "$RK_CHIP_DIR/$RK_AMP_CFG" ]; then
		set -a
		source "$RK_CHIP_DIR/$RK_AMP_CFG"
		set +a
	fi

	if [ "$RK_AMP_RTT_TARGET" ]; then
		rtt_dir="$RK_RTOS_BSP_DIR/$RK_AMP_RTT_TARGET"
		[ -d "$rtt_dir" ] && cd "$rtt_dir" && scons -c >/dev/null || true
	fi

	if [ "$RK_AMP_HAL_TARGET" ]; then
		hal_dir="$RK_RTOS_BSP_DIR/common/hal/project/$RK_AMP_HAL_TARGET/GCC"
		if [ ! -d "$hal_dir" ] && [ "$RK_AMP_HAL_TARGET" = "ok3506" ]; then
			hal_dir="$RK_RTOS_BSP_DIR/common/hal/project/rk3506/GCC"
		fi
		[ -d "$hal_dir" ] && cd "$hal_dir" && make clean >/dev/null || true
	fi

	if [ "$RK_AMP_ZEPHYR_BUILD_DIR" ]; then
		zephyr_build_dir="$(amp_resolve_path "$RK_AMP_ZEPHYR_BUILD_DIR")"
		[ -d "$zephyr_build_dir" ] && rm -rf "$zephyr_build_dir"
	fi

	# Also clean the prebuilt Zephyr cache so the next build
	# does a full source rebuild.
	if [ "$RK_AMP_ZEPHYR_PREBUILT_BIN" ]; then
		zephyr_prebuilt_bin="$(amp_resolve_path "$RK_AMP_ZEPHYR_PREBUILT_BIN")"
		[ -f "$zephyr_prebuilt_bin" ] && rm -f "$zephyr_prebuilt_bin"
	fi

	rm -rf "$RK_FIRMWARE_DIR/amp.img"
}

build_images()
{
	for item in $1
	do
		ITS_IMAGE=$(amp_get_node "$(cat $ITS_FILE)" $item)

		# update all parameters
		FIRMWARE_CPU_BASE=$(amp_get_value "$ITS_IMAGE" load)
		DRAM_SIZE=$(amp_get_value "$ITS_IMAGE" size)
		SRAM_BASE=$(amp_get_value "$ITS_IMAGE" srambase)
		SRAM_SIZE=$(amp_get_value "$ITS_IMAGE" sramsize)
		CUR_CPU=$(amp_get_value "$ITS_IMAGE" cpu)
		CPU_BIN=$(amp_get_string "$ITS_IMAGE" data)
		if (( $CUR_CPU > 0xff )); then
			CUR_CPU=$((CUR_CPU >> 8))
		fi
		CUR_CPU=$(($CUR_CPU))

		echo Image info: $item
		for p in FIRMWARE_CPU_BASE DRAM_SIZE SRAM_BASE SRAM_SIZE SHMEM_BASE \
			 SHMEM_SIZE LINUX_RPMSG_BASE LINUX_RPMSG_SIZE CUR_CPU
		do
			echo $(env | grep -w $p && true)
		done

		SYS=$(amp_get_string "$ITS_IMAGE" sys)
		CORE=$(amp_get_string "$ITS_IMAGE" core)
		ZEPHYR_APP=$(amp_get_string "$ITS_IMAGE" zephyr_app)
		ZEPHYR_BOARD=$(amp_get_string "$ITS_IMAGE" zephyr_board)
		ZEPHYR_BOARD_ROOT=$(amp_get_string "$ITS_IMAGE" zephyr_board_root)
		ZEPHYR_SOC_ROOT=$(amp_get_string "$ITS_IMAGE" zephyr_soc_root)
		ZEPHYR_DTS_ROOT=$(amp_get_string "$ITS_IMAGE" zephyr_dts_root)
		ZEPHYR_DIR=$(amp_get_string "$ITS_IMAGE" zephyr_dir)
		ZEPHYR_PREBUILT_BIN=$(amp_get_string "$ITS_IMAGE" zephyr_prebuilt_bin)
		ZEPHYR_CONF=$(amp_get_string "$ITS_IMAGE" zephyr_conf)

		# In RTT: 'CC' means the directory where the GCC tools are located.
		# In HAL: 'CC' means the directory and the prefix of GCC.
		CC=$(amp_get_string "$ITS_IMAGE" cc)
		[ ! -n "$CC" ] || CC="${RK_SDK_DIR}/${CC}"

		SYS="${SYS}${CORE:+_$CORE}"

		case $SYS in
			hal_mcu)
				build_hal RK_AMP_MCU_HAL_TARGET mcu \
					  "$(basename -s .bin $CPU_BIN)"
				;;
			hal|hal_ap)
				build_hal RK_AMP_HAL_TARGET $CUR_CPU \
					  "$(basename -s .bin $CPU_BIN)"
				;;
			rtt_mcu)
				build_rtthread RK_AMP_MCU_RTT_TARGET mcu \
					       "$(basename -s .bin $CPU_BIN)" \
					       "$(amp_get_string "$ITS_IMAGE" rtt_config)"
				;;
			rtt|rtt_ap)
				build_rtthread RK_AMP_RTT_TARGET $CUR_CPU \
					       "$(basename -s .bin $CPU_BIN)" \
					       "$(amp_get_string "$ITS_IMAGE" rtt_config)" \
				;;
			zephyr|zephyr_ap)
				build_zephyr $CUR_CPU \
					     "$(basename -s .bin $CPU_BIN)" \
					     "$ZEPHYR_APP" \
					     "$ZEPHYR_BOARD" \
					     "$ZEPHYR_BOARD_ROOT" \
					     "$ZEPHYR_DIR" \
					     "$ZEPHYR_PREBUILT_BIN" \
					     "$ZEPHYR_CONF" \
					     "$ZEPHYR_SOC_ROOT" \
					     "$ZEPHYR_DTS_ROOT"
				;;
			*)
				break;;
		esac
	done
}

BUILD_CMDS="amp"
build_hook()
{
	local i

	check_config RK_AMP || false

	message "=========================================="
	message "          Start building AMP"
	message "=========================================="

	"$RK_SCRIPTS_DIR/check-amp.sh"

	export CROSS_COMPILE=$(get_toolchain AMP "$RK_AMP_ARCH" "" none)
	[ "$CROSS_COMPILE" ] || exit 1

	if [ -f "$RK_CHIP_DIR/$RK_AMP_CFG" ]; then
		set -a
		source $RK_CHIP_DIR/$RK_AMP_CFG
		set +a
	fi

	CORE_NUMBERS=$(grep -wcE "amp[0-9]* {|mcu {" $ITS_FILE)
	echo "CORE_NUMBERS=$CORE_NUMBERS"

	EXT_SHARE=$(amp_get_node "$(cat $ITS_FILE)" share)
	if [ "$EXT_SHARE" ]; then
		SHMEM_BASE=$(amp_get_value "$EXT_SHARE" "shm_base")
		if [ "$SHMEM_BASE" ]; then
			export SHMEM_BASE
			export SHMEM_SIZE=$(amp_get_value "$EXT_SHARE" "shm_size")
			AMP_PRIMARY_CORE=$(amp_get_value "$EXT_SHARE" primary)
			[ ! $AMP_PRIMARY_CORE ] || export AMP_PRIMARY_CORE=$(($AMP_PRIMARY_CORE))
		fi

		LINUX_RPMSG_BASE=$(amp_get_value "$EXT_SHARE" "rpmsg_base")
		if [ "$LINUX_RPMSG_BASE" ]; then
			export LINUX_RPMSG_BASE=$LINUX_RPMSG_BASE
			export LINUX_RPMSG_SIZE=$(amp_get_value "$EXT_SHARE" "rpmsg_size")
		fi
	fi

	ITS_IMAGES=$(grep -wE "amp[0-9]* {|mcu {" $ITS_FILE | grep -oE "amp[0-9]*|mcu")
	build_images "$ITS_IMAGES"

	cd "$RK_OUTDIR"
	ln -rsf $ITS_FILE amp.its
	sed -i '/share {/,/}/d' amp.its
	sed -i '/compile {/,/}/d' amp.its

	$RK_RTOS_BSP_DIR/tools/mkimage -f amp.its -E -p 0xe00 $RK_FIRMWARE_DIR/amp.img

	finish_build amp $@
}

source "${RK_BUILD_HELPER:-$(dirname "$(realpath "$0")")/../build-hooks/build-helper}"

build_hook $@
