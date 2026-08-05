#!/bin/bash
# A11 batch 2: the -T3F stage-B TARGET ADMISSION scan (upstream t3dnn fake score) on the
# assembled baseline, plus the no-op gate for the second edit.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/a11_ref/a11_run.sh"
bash "$R" GATE4  -XCD 2                 &
bash "$R" F005   -XCD 2 -T3F 0.05       &
bash "$R" F010   -XCD 2 -T3F 0.10       &
bash "$R" F020   -XCD 2 -T3F 0.20       &
bash "$R" F040   -XCD 2 -T3F 0.40       &
wait
echo "[a11] BATCH 2 COMPLETE"
