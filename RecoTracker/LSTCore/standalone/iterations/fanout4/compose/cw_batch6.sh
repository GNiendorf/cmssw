#!/bin/bash
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/cheapwins
run() { "$P/cw_run.sh" "$@" > "$P/drv_$1.txt" 2>&1; }
run e1 -TR 1 -TT 3.0 -F 0.25 -MRI 0.25 -PU 1 &
run e2 -TR 1 -TT 3.0 -F 0.25 -MRI 0.0 &
run e3 -TR 1 -TT 3.0 -F 0.22 -MRI 0.25 &
run e4 -TR 1 -TT 3.0 -F 0.20 -MRI 0.25 -M4 3.0 &
run e5 -TR 1 -TT 2.0 -F 0.22 -MRI 0.25 &
run e6 -TR 1 -TT 3.0 -F 0.25 -MRI 0.25 -M4 3.2 &
run e7 -TR 1 -TT 3.0 -F 0.25 -MRI 0.25 -TA 1.0 &
run e8 -TR 1 -TT 1.5 -F 0.25 -MRI 0.25 &
wait
echo "BATCH 6 DONE"
