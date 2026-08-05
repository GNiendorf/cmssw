#!/bin/bash
# okretrain scan driver: run a list of "TAG|BIN|FLAGS" specs, MAXPAR at a time.
#   ok_scan.sh <specfile> [maxpar]
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/okretrain
SPEC="$1"; MAXPAR="${2:-6}"
n=0
while IFS='|' read -r TAG BIN FLAGS; do
  [ -z "$TAG" ] && continue
  case "$TAG" in \#*) continue;; esac
  BIN="$BIN" bash "$P/ok_ab.sh" "$TAG" $FLAGS > "$P/run_${TAG}.out" 2>&1 &
  n=$((n+1))
  if [ $((n % MAXPAR)) -eq 0 ]; then wait; fi
done < "$SPEC"
wait
echo "SCAN DONE: $n runs"
