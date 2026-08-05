#!/bin/bash
# a13_batch.sh <<'EOF'  ... lines of "TAG :: overrides" ... EOF
# Launches every line in parallel, waits for all, prints a one-line summary each.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/a13_ref"
pids=()
while IFS= read -r line; do
  [ -z "$line" ] && continue
  tag="${line%% ::*}"
  ov="${line#*:: }"
  bash "$P/a13_run.sh" "$tag" $ov > "$P/run_${tag}.out" 2>&1 &
  pids+=($!)
  echo "[a13] launched $tag : $ov"
done
for p in "${pids[@]}"; do wait "$p"; done
echo "[a13] BATCH COMPLETE"
