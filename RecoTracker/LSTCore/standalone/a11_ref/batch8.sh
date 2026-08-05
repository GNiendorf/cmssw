#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/a11_ref/a11_run.sh"
bash "$R" R_F10R50X375 -XCD 2 -T3F 0.10 -RPSA 5.0 -XCT 3.75            &
bash "$R" R_F05R50     -XCD 2 -T3F 0.05 -RPSA 5.0                      &
bash "$R" R_F20R50     -XCD 2 -T3F 0.20 -RPSA 5.0                      &
bash "$R" C2_A60R50    -XCD 2 -T3F 0.10 -RPSA 5.0 -RPST 6 -a 6.0       &
bash "$R" R_F10R50T5   -XCD 2 -T3F 0.10 -RPSA 5.0 -RPST 5.0            &
wait
echo "[a11] BATCH 8 COMPLETE"
