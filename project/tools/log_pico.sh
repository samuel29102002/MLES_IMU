#!/usr/bin/env bash
set -euo pipefail
PORT=${1:-$(ls /dev/cu.usbmodem* 2>/dev/null | head -n 1)}
LABEL=${2:-session}
OUT="logs/${LABEL}_$(date +%Y%m%d_%H%M%S).csv"
HEADER="t_ms,ax,ay,az,gx,gy,gz,amag_mean,amag_std,amag_rms,energy,dom_freq,bp1,bp2,gx_std,gy_std,gz_std,d_pitch_std,d_roll_std,class,lat_ms,q_len"
mkdir -p logs
echo "Logging from ${PORT:-<none>} -> $OUT"
if [[ -z "${PORT:-}" ]]; then echo "No /dev/cu.usbmodem* found"; exit 1; fi
( echo "$HEADER" && cat "$PORT" | grep -v 'GESTURE:' ) > "$OUT"
