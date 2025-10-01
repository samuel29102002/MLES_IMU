#!/usr/bin/env bash
set -euo pipefail
IN="${1:-}"; [[ -f "$IN" ]] || { echo "Usage: $0 <path/to/log.csv>"; exit 1; }
OUT="${IN%.csv}_clean.csv"
HEADER="t_ms,ax,ay,az,gx,gy,gz,amag_mean,amag_std,amag_rms,energy,dom_freq,bp1,bp2,gx_std,gy_std,gz_std,d_pitch_std,d_roll_std,class,lat_ms,q_len"
grep -v -E 'GESTURE:|WARN:' "$IN" | tr '\t' ',' > "$OUT.tmp"
# add header if missing
head -n 1 "$OUT.tmp" | grep -q '^[0-9-]' && { echo "$HEADER" | cat - "$OUT.tmp" > "$OUT"; } || mv "$OUT.tmp" "$OUT"
rm -f "$OUT.tmp"
echo "Wrote $OUT"
