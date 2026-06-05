#!/bin/sh

set -eu

SECONDS_TO_RUN="${1:-28800}"
INTERVAL_US="${2:-0}"
PROGRESS_SECONDS="${3:-60}"
LOG_DIR="/userdata/rk3506-cache-stress"
STAMP="$(date +%Y%m%d-%H%M%S 2>/dev/null || echo unknown)"
LOG_FILE="$LOG_DIR/cache-stress-$STAMP.log"
DMESG_FILE="$LOG_FILE.dmesg"
STATUS_FILE="$LOG_FILE.status"

mkdir -p "$LOG_DIR"

echo "[RK3506][CACHE-STRESS] duration=${SECONDS_TO_RUN}s interval_us=${INTERVAL_US} progress=${PROGRESS_SECONDS}s"
echo "[RK3506][CACHE-STRESS] log=$LOG_FILE"

if [ ! -x /userdata/rk3506_rpmsg_char_stress ]; then
	echo "[RK3506][CACHE-STRESS] missing /userdata/rk3506_rpmsg_char_stress" >&2
	exit 1
fi

sync

rm -f "$STATUS_FILE"
(
	set +e
	/userdata/rk3506_rpmsg_char_stress \
		-s "$SECONDS_TO_RUN" \
		-i "$INTERVAL_US" \
		-p "$PROGRESS_SECONDS"
	echo "$?" > "$STATUS_FILE"
) 2>&1 | tee "$LOG_FILE"
rc="$(cat "$STATUS_FILE" 2>/dev/null || echo 1)"
rm -f "$STATUS_FILE"

dmesg > "$DMESG_FILE" 2>/dev/null || true
sync

echo "[RK3506][CACHE-STRESS] rc=$rc"
echo "[RK3506][CACHE-STRESS] log=$LOG_FILE"
echo "[RK3506][CACHE-STRESS] dmesg=$DMESG_FILE"

exit "$rc"
