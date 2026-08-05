#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/a11_ref/a11_run.sh"
P="-RPSA 6.875 -RPST 6"
bash "$R" C_A60X375 -XCD 2 -T3F 0.10 $P -a 6.0 -XCT 3.75 &
bash "$R" C_A55     -XCD 2 -T3F 0.10 $P -a 5.5           &
wait
echo "[a11] BATCH 6 COMPLETE"
