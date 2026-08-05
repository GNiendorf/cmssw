#!/bin/bash
# c3_frontier batch driver: run several A/Bs in parallel (foreground wait).
# Usage: c3_batch.sh "<tag>|<extra args>" "<tag>|<extra args>" ...
# Common j1 skeleton is prepended automatically.
set -u
DIR=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout2/c3_frontier
SKEL="-e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M5 1e9 -M6 1e9 -U4 0 -U5 0 -U6 0 -MD 1e9 -B 10 -H 1 -W 0.25"
pids=()
for spec in "$@"; do
  tag="${spec%%|*}"
  args="${spec#*|}"
  rm -f "$DIR/ab_${tag}.root" "$DIR/ab_${tag}_hists.root"
  bash "$DIR/run_ab.sh" "$tag" $SKEL $args > "$DIR/batch_${tag}.out" 2>&1 &
  pids+=($!)
done
fail=0
for p in "${pids[@]}"; do wait "$p" || fail=1; done
echo "batch done fail=$fail"
