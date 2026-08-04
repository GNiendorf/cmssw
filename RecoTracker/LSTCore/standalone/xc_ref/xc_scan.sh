#!/bin/bash
# xc_scan.sh -- SEED CROSSCLEAN threshold scan + per-branch ablation.
# Usage: xc_scan.sh <maxParallel> <spec> [<spec> ...]   with spec = TAG:args...
# (args are passed verbatim to xc_run.sh after the frozen POSTDELP2 line).
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
NPAR="$1"; shift
i=0
for spec in "$@"; do
  TAG="${spec%%:*}"
  ARGS="${spec#*:}"
  bash "$S/xc_ref/xc_run.sh" "$TAG" $ARGS > "$S/xc_ref/run_${TAG}.out" 2>&1 &
  i=$((i+1))
  if [ $((i % NPAR)) -eq 0 ]; then wait; fi
done
wait
echo SCAN_ALLDONE
