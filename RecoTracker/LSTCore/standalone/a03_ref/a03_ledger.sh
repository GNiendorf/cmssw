#!/bin/bash
# Delivery ledger + prefilter volume for a set of A03 tags.
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a03_ref
for t in "$@"; do
  echo "=== $t ==="
  grep -E "A03 prefilter|A03 stage-B margin" "$P/r_$t.log" 2>/dev/null
  grep -E "M20 candfind" "$P/r_$t.log" 2>/dev/null | sed 's/.*| examined/  examined/'
  grep -E "M20 pT3 dedup|M20 stageB|XC crossclean|M16 delivery|output TCs" "$P/r_$t.log" 2>/dev/null
done
