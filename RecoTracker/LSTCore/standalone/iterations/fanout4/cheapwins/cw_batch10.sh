#!/bin/bash
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/cheapwins
run() { "$P/cw_run.sh" "$@" > "$P/drv_$1.txt" 2>&1; }
H7="-TR 1 -TT 1.5 -F 0.20 -MRI 0.25 -M4 3.0 -TA 1.0 -PU 1 -B 20"
run k1 $H7 -MRI 0.0 &
run k2 $H7 -MRI -0.25 &
run k3 $H7 -MRI -0.5 &
run k4 $H7 -MRI 0.0 -TA 0.5 &
run k5 $H7 -MRI -0.25 -TA 0.5 &
run k6 $H7 -TT 1.0 &
run k7 $H7 -MRI -0.25 -M4 3.2 &
run k8 $H7 -MRI -0.25 -TA 1.5 &
wait
echo "BATCH 10 DONE"
