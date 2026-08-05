#!/bin/bash
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/cheapwins
run() { "$P/cw_run.sh" "$@" > "$P/drv_$1.txt" 2>&1; }
run f1 -TR 1 -TT 3.0 -F 0.20 -MRI 0.25 -M4 3.0 -TA 1.0 &
run f2 -TR 1 -TT 3.0 -F 0.18 -MRI 0.25 -M4 3.0 -TA 1.0 &
run f3 -TR 1 -TT 3.0 -F 0.25 -MRI 0.25 -TA 1.0 -M4 3.0 &
run f4 -TR 1 -TT 3.0 -F 0.25 -MRI 0.25 -TA 1.0 -M4 3.0 -PU 1 &
run f5 -TR 1 -TT 2.0 -F 0.20 -MRI 0.25 -M4 3.0 -TA 1.0 &
run f6 -TR 1 -TT 3.0 -F 0.25 -MRI 0.25 -TA 1.0 -PU 0 &
run f7 -TR 1 -TT 3.0 -F 0.22 -MRI 0.25 -M4 2.5 -TA 1.0 &
run f8 -TR 1 -TT 3.0 -F 0.25 -MRI 0.25 -TA 0.5 &
wait
echo "BATCH 7 DONE"
