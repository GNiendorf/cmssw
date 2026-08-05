#!/bin/bash
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/cheapwins
run() { "$P/cw_run.sh" "$@" > "$P/drv_$1.txt" 2>&1; }
K6="-TR 1 -TT 1.0 -F 0.20 -MRI 0.25 -M4 3.0 -TA 1.0 -PU 1 -B 20"
run m1 $K6 -M4 3.2 &
run m2 $K6 -M4 3.5 &
run m3 $K6 -M4 3.2 -MRI -0.25 &
run m4 $K6 -M4 3.2 -MRI -0.5 &
run m5 $K6 -TA 1.5 &
run m6 $K6 -TA 1.5 -MRI -0.25 &
run m7 $K6 -M4 3.2 -MRI -0.25 -B 30 &
run m8 $K6 -TT 0.8 -M4 3.2 -MRI -0.25 &
wait
echo "BATCH 11 DONE"
