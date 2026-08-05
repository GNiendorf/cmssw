#!/bin/bash
# Print the A14 per-region ledger blocks from one or more run logs.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a14_ref
for t in "$@"; do
  echo "===== $t ====="
  grep -E "^  (RG |XC truth|XC crossclean|M20 pT3 dedup|M20 stageB)" "$S/r_${t}.log" 2>/dev/null
done
