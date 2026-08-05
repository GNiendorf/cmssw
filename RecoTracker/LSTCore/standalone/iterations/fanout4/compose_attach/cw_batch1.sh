#!/bin/bash
# Phase-1 cheap-lever scan. Runs in parallel batches of 5.
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/cheapwins
run() { "$P/cw_run.sh" "$@" > "$P/drv_$1.txt" 2>&1; }

# batch A: -PU lever + M4 loosening
run pu0    -PU 0 &
run pu1    -PU 1 &
run m4_30  -M4 3.0 &
run m4_25  -M4 2.5 &
run m4_20  -M4 2.0 &
wait
echo "BATCH A DONE"

# batch B: MRI loosening + the two task-requested tightening points
run mri_00  -MRI 0.0 &
run mri_m05 -MRI -0.5 &
run mri_m10 -MRI -1.0 &
run m4_40   -M4 4.0 &
run mri_10  -MRI 1.0 &
wait
echo "BATCH B DONE"
