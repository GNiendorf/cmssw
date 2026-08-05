#!/bin/bash
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/cheapwins
run() { "$P/cw_run.sh" "$@" > "$P/drv_$1.txt" 2>&1; }
run ok2  -OK 2 &
run w0   -W 0 &
run f035 -F 0.35 &
wait
echo "BATCH 2 DONE"
