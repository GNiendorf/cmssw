#!/bin/bash
# A11 batch 1: no-op gate for the -RPSA/-RPST edit, then the ATTACH-MARGIN scan with the
# seed-retirement margin PINNED at the baseline value 6.875 (so -a moves alone).
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/a11_ref/a11_run.sh"
bash "$R" GATE2  -XCD 2                                &
bash "$R" GATE3  -XCD 2 -RPSA 6.875 -RPST 6            &
bash "$R" A55    -XCD 2 -RPSA 6.875 -RPST 6 -a 5.5     &
bash "$R" A50    -XCD 2 -RPSA 6.875 -RPST 6 -a 5.0     &
bash "$R" A40    -XCD 2 -RPSA 6.875 -RPST 6 -a 4.0     &
bash "$R" A30    -XCD 2 -RPSA 6.875 -RPST 6 -a 3.0     &
bash "$R" A40C   -XCD 2 -a 4.0                         &
wait
echo "[a11] BATCH 1 COMPLETE"
