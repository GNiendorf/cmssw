#!/bin/bash
# print the XC / M20 / reach summary lines for a tag
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a01_ref
for t in "$@"; do
  echo "=== $t ==="
  grep -E "XC crossclean|XC ownarm|XC truth|XC reach|M20 pT3 dedup|M20 stageB" "$P/r_${t}.log" 2>/dev/null
done
