#!/bin/bash
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/cheapwins
run() { "$P/cw_run.sh" "$@" > "$P/drv_$1.txt" 2>&1; }
Q="-TR 1 -TT 0.8 -F 0.20 -MRI -0.5 -TA 1.0 -PU 1 -B 30"
run s1 $Q -C25 0.5 -M4 3.6 &
run s2 $Q -C25 0.0 -M4 3.6 &
run s3 $Q -C25 0.5 -M4 3.8 &
run s4 $Q -C25 0.0 -M4 3.8 &
run s5 $Q -C25 0.5 -M4 3.6 -TT 0.9 &
run s6 $Q -C25 -0.5 -M4 3.8 &
run s7 $Q -C25 0.5 -M4 3.6 -C25D -2.5 &
run s8 $Q -C25 0.0 -M4 4.0 &
wait
echo "BATCH 16 DONE"
