#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/a11_ref/a11_run.sh"
bash "$R" F10X375 -XCD 2 -T3F 0.10 -XCT 3.75  &
bash "$R" F15     -XCD 2 -T3F 0.15            &
bash "$R" F15X375 -XCD 2 -T3F 0.15 -XCT 3.75  &
bash "$R" F07     -XCD 2 -T3F 0.07            &
wait
echo "[a11] BATCH 4 COMPLETE"
