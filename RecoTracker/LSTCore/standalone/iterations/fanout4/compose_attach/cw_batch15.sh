#!/bin/bash
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/cheapwins
run() { "$P/cw_run.sh" "$@" > "$P/drv_$1.txt" 2>&1; }
Q1="-TR 1 -TT 0.8 -F 0.20 -MRI -0.5 -M4 3.4 -TA 1.0 -PU 1 -B 30 -C25 1.0"
run r1 $Q1 -TT 0.9 &
run r2 $Q1 -M4 3.6 &
run r3 $Q1 -C25 1.5 &
run r4 $Q1 -TA 1.2 &
run r5 $Q1 -C25 0.5 &
run r6 $Q1 -C25D -2.5 &
run r7 $Q1 -C25 0.5 -TT 0.9 -M4 3.6 &
run r8 $Q1 -C25 1.5 -TT 0.9 &
wait
echo "BATCH 15 DONE"
