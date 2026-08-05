#!/bin/bash
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/cheapwins
run() { "$P/cw_run.sh" "$@" > "$P/drv_$1.txt" 2>&1; }
S2="-TR 1 -TT 0.8 -F 0.20 -MRI -0.5 -M4 3.6 -TA 1.0 -PU 1 -B 30 -C25 0.0"
run t1 $S2 -C25 -1.0 -M4 4.0 &
run t2 $S2 -MRI -1.0 &
run t3 $S2 -M4D -1.2 &
run t4 $S2 -PU 0 &
run t5 $S2 -M4 4.0 -M4D -1.2 &
run t6 $S2 -C25 -0.5 -M4 4.0 -TA 1.2 &
wait
echo "BATCH 17 DONE"
