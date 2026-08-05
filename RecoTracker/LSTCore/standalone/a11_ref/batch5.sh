#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/a11_ref/a11_run.sh"
P="-RPSA 6.875 -RPST 6"
bash "$R" A60     -XCD 2 $P -a 6.0                &
bash "$R" A55     -XCD 2 $P -a 5.5                &
bash "$R" A50D2   -XCD 2 $P -a 5.0 -D4 2.0        &
bash "$R" C_F10A50 -XCD 2 -T3F 0.10 $P -a 5.0     &
bash "$R" C_F10A60 -XCD 2 -T3F 0.10 $P -a 6.0     &
wait
echo "[a11] BATCH 5 COMPLETE"
