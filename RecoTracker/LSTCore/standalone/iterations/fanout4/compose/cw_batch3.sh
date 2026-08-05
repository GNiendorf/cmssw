#!/bin/bash
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/cheapwins
run() { "$P/cw_run.sh" "$@" > "$P/drv_$1.txt" 2>&1; }
# dup/fake-reducing levers to pay for the margin loosening
run tr1    -TR 1 &
run tr1ta3 -TR 1 -TA 3.0 &
run f025   -F 0.25 &
run fc0    -FC 0 &
wait
echo "BATCH 3 DONE"
