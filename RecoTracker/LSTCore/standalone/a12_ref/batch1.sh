#!/bin/bash
# A12 batch 1 -- CHAIN-ATTACH RECALL scan. The assembled baseline moves ONE existing
# constant: -a (thetaAttach), the chain-class attach-head logit threshold.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
B="-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2 -XCD 2"
run() { bash $S/a12_ref/a12_run.sh "$@" ; }
run A_a80 $B -a 8.0   &
run A_a60 $B -a 6.0   &
run A_a50 $B -a 5.0   &
run A_a40 $B -a 4.0   &
run A_a30 $B -a 3.0   &
run A_a20 $B -a 2.0   &
wait
echo "[a12] BATCH1 COMPLETE"
