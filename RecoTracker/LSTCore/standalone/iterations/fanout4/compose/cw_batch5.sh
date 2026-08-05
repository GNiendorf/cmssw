#!/bin/bash
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/cheapwins
run() { "$P/cw_run.sh" "$@" > "$P/drv_$1.txt" 2>&1; }
run d1 -TR 1 -TT 3.0 -F 0.25 &
run d2 -TR 1 -TT 2.0 -F 0.25 &
run d3 -TR 1 -TT 3.0 -F 0.22 &
run d4 -TR 1 -TT 3.0 -F 0.25 -MRI 0.25 &
run d5 -TR 1 -TT 3.0 -F 0.25 -PU 1 &
run d6 -TR 1 -TT 3.0 -F 0.25 -M4 3.0 &
run d7 -TR 1 -TT 2.0 -F 0.22 &
run d8 -TR 1 -TT 3.0 -TP 2 -F 0.25 &
wait
echo "BATCH 5 DONE"
