#!/bin/bash
# A11 batch 3: extend the -T3F result. (a) a tighter admission point, (b) buy the small
# duplicate cost back with -XCT, (c) spend the freed purity on a LOOSER delivery margin
# -AT3 (the whole point of admitting on target quality: the head no longer has to carry
# the junk-rejection burden alone).
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/a11_ref/a11_run.sh"
bash "$R" F002    -XCD 2 -T3F 0.02             &
bash "$R" F005X35 -XCD 2 -T3F 0.05 -XCT 3.5    &
bash "$R" F005A5  -XCD 2 -T3F 0.05 -AT3 5      &
bash "$R" F005A4  -XCD 2 -T3F 0.05 -AT3 4      &
bash "$R" F010X35 -XCD 2 -T3F 0.10 -XCT 3.5    &
wait
echo "[a11] BATCH 3 COMPLETE"
