#!/bin/bash
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/cheapwins
run() { "$P/cw_run.sh" "$@" > "$P/drv_$1.txt" 2>&1; }
N6="-TR 1 -TT 0.8 -F 0.20 -MRI -0.25 -M4 3.4 -TA 1.0 -PU 1 -B 30"
run p1 $N6 -MRI -0.5 &
run p2 $N6 -TA 0.8 &
run p3 $N6 -M4 3.3 &
run p4 $N6 -TT 0.9 &
wait
echo "BATCH 13 DONE"
