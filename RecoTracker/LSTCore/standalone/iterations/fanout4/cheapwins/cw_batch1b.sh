#!/bin/bash
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/cheapwins
run() { "$P/cw_run.sh" "$@" > "$P/drv_$1.txt" 2>&1; }
run mri_025 -MRI 0.25 &
run m4_15   -M4 1.5 &
run mri_m025 -MRI -0.25 &
wait
echo "BATCH 1b DONE"
