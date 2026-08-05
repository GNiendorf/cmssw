#!/bin/bash
# A06 BATCH 1b -- the two runs that were killed by process-group cleanup, relaunched as
# tracked children (no nohup, no detach).
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/a06_ref/a06_run.sh"
bash "$R" GATE            &
bash "$R" L_L5   -L 5.0   &
wait
echo "[a06] BATCH1b COMPLETE"
