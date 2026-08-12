#!/bin/bash
S=/mnt/data1/gsn27/here/gpu_wt/g4/src/RecoTracker/LSTCore/standalone
W=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/nnloop_ref/s3_work
cd $S && source setup.sh >/dev/null 2>&1 && eval $(scramv1 runtime -sh) >/dev/null 2>&1 && source setup.sh >/dev/null 2>&1
cd $S
export LST_CHAIN_JOIN_DUMP=$W/join.bin
unset LST_CHAIN_PAIR_DUMP
./bin/lst_cpu -i PU200RelVal -n 1000 -s 1 -p 0.8 -w 0 -o $W/runs/joinrun.root > $W/logs/joinrun.log 2>&1
echo "RUN_EXIT=$?" >> $W/logs/joinrun.log
