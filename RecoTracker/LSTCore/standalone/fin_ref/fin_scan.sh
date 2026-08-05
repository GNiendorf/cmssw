#!/bin/bash
# fin_scan.sh -- parallel point runner. Usage: fin_scan.sh <maxParallel> <spec> ...
# spec = TAG:args...  (args passed verbatim to fin_run.sh after the frozen line).
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
NPAR="$1"; shift
i=0
for spec in "$@"; do
  TAG="${spec%%:*}"
  ARGS="${spec#*:}"
  bash "$S/fin_ref/fin_run.sh" "$TAG" $ARGS > "$S/fin_ref/run_${TAG}.out" 2>&1 &
  i=$((i+1))
  if [ $((i % NPAR)) -eq 0 ]; then wait; fi
done
wait
echo SCAN_ALLDONE
