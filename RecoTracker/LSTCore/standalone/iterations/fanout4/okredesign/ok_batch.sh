#!/bin/bash
# ok_batch.sh <jobsfile>
# Each line of <jobsfile>: <tag> <extra chainproto args...>
# Runs every line in PARALLEL (one chainproto each), then scores each.
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/okredesign
JOBS="$1"
pids=""
while read -r line; do
  [ -z "$line" ] && continue
  case "$line" in \#*) continue;; esac
  bash "$P/ok_run.sh" $line > "$P/run_$(echo "$line" | awk '{print $1}').out" 2>&1 &
  pids="$pids $!"
done < "$JOBS"
for p in $pids; do wait $p; done
echo "BATCH DONE"
