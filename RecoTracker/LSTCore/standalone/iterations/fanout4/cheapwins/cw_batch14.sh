#!/bin/bash
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/cheapwins
run() { "$P/cw_run.sh" "$@" > "$P/drv_$1.txt" 2>&1; }
P1="-TR 1 -TT 0.8 -F 0.20 -MRI -0.5 -M4 3.4 -TA 1.0 -PU 1 -B 30"
run q1 $P1 -C25 1.0 &
run q2 $P1 -C25 -1e9 &
run q3 $P1 -M4D -1.5 &
run q4 $P1 -TT 0.9 -M4 3.0 &
wait
echo "BATCH 14 DONE"
