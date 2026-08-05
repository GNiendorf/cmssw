#!/bin/bash
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/cheapwins
run() { "$P/cw_run.sh" "$@" > "$P/drv_$1.txt" 2>&1; }
M7="-TR 1 -TT 1.0 -F 0.20 -MRI -0.25 -M4 3.2 -TA 1.0 -PU 1 -B 30"
run n1 $M7 -TT 0.8 &
run n2 $M7 -TT 0.8 -M4 3.5 &
run n3 $M7 -TT 0.8 -TA 1.5 &
run n4 $M7 -MRI -0.5 &
run n5 $M7 -B 40 &
run n6 $M7 -TT 0.8 -M4 3.4 &
run n7 $M7 -TT 0.5 -M4 3.5 &
run n8 $M7 -TT 0.8 -e 0.25 &
wait
echo "BATCH 12 DONE"
