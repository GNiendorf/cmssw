#!/bin/bash
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/cheapwins
run() { "$P/cw_run.sh" "$@" > "$P/drv_$1.txt" 2>&1; }
run c1  -TR 1 -F 0.25 &
run c2  -TR 1 -TA 3.0 -F 0.25 &
run c3  -TR 1 -F 0.25 -MRI 0.0 &
run c4  -TR 1 -M4 4.0 &
run c5  -TR 1 -F 0.28 &
run tt3 -TR 1 -TT 3.0 &
run tt8 -TR 1 -TT 8.0 &
run tp2 -TR 1 -TP 2 &
wait
echo "BATCH 4 DONE"
