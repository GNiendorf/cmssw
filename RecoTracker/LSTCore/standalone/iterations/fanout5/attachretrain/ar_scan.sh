#!/bin/bash
# ar_scan.sh "<tag>:<overrides>" ["<tag>:<overrides>" ...]
# Runs each FLAGSHIP-based point with the attach confusion instrument armed, in parallel.
# Every point is FLAGSHIP + the listed overrides (which win, being appended last).
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout5/attachretrain"
JOBS="${AR_JOBS:-8}"
for spec in "$@"; do
  echo "$spec"
done | xargs -P "$JOBS" -I{} bash -c '
  spec="{}"; tag="${spec%%:*}"; ov="${spec#*:}"
  AR_CM=1 '"$P"'/ar_run.sh "$tag" fl $ov
'
echo "=== SCAN COMPLETE ==="
