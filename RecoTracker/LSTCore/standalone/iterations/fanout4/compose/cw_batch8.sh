#!/bin/bash
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/cheapwins
run() { "$P/cw_run.sh" "$@" > "$P/drv_$1.txt" 2>&1; }
F5="-TR 1 -TT 2.0 -F 0.20 -MRI 0.25 -M4 3.0"
run g1 $F5 -TA 2.0 &
run g2 $F5 -TA 3.0 &
run g3 $F5 -TA 1.0 -FC -1 &
run g4 $F5 -TA 1.0 -MRI 0.0 &
run g5 $F5 -TA 1.0 -PU 1 &
run g6 $F5 -TA 1.0 -B 20 &
run g7 $F5 -TA 1.0 -TL 6 &
run g8 -TR 1 -TT 2.0 -F 0.20 -MRI 0.25 -M4 2.5 -TA 2.0 &
wait
echo "BATCH 8 DONE"
