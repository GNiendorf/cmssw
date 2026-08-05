#!/bin/bash
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/cheapwins
run() { "$P/cw_run.sh" "$@" > "$P/drv_$1.txt" 2>&1; }
F5="-TR 1 -TT 2.0 -F 0.20 -MRI 0.25 -M4 3.0 -TA 1.0"
run h1 $F5 -PU 1 -B 20 &
run h2 $F5 -PU 1 -B 20 -MRI 0.0 &
run h3 $F5 -PU 1 -B 30 &
run h4 $F5 -PU 1 -B 20 -MRI 0.0 -TA 0.5 &
run h5 $F5 -PU 1 -B 20 -MRI 0.0 -M4 2.5 &
run h6 $F5 -PU 1 -B 20 -FC -1 -F 0.30 &
run h7 $F5 -PU 1 -B 20 -TT 1.5 &
run h8 $F5 -PU 1 -B 20 -MRI 0.0 -TT 3.0 &
wait
echo "BATCH 9 DONE"
