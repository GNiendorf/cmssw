#!/bin/bash
# Pull the A12-relevant per-event counters out of a run log.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
for t in "$@"; do
  L="$S/a12_ref/r_${t}.log"
  [ -f "$L" ] || L="$S/fin_ref/r_${t}.log"
  [ -f "$L" ] || { echo "$t: no log"; continue; }
  echo "=== $t ==="
  grep -E 'A12 recall|A12 attach|M16 delivery|M16 suppression|output TCs/evt|XC crossclean|M20 pT3 dedup|M16 attach' "$L"
done
