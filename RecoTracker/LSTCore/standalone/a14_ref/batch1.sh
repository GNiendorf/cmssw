#!/bin/bash
# A14 batch 1: the per-region calibration probes. BASE = the assembled baseline.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/a14_ref/a14_run.sh"
export BIN="$S/protoA14/bin/chainproto_v1"
B="-XC 3 -T3E 1 -CC 1 -CCN 1 -CCR 2"
# tag                                   XCT bins            AT3 bins
bash $R A0    $B -XCT 4 -XCT2 4 -XCT3 4   -RGD 1 -XCD 2 &
bash $R XE45  $B -XCT 4 -XCT2 4 -XCT3 4.5 -RGD 1 &
bash $R XE5   $B -XCT 4 -XCT2 4 -XCT3 5   -RGD 1 &
bash $R XB35  $B -XCT 3.5 -XCT2 4 -XCT3 4 -RGD 1 &
bash $R XB3   $B -XCT 3 -XCT2 4 -XCT3 4   -RGD 1 &
bash $R XBT3  $B -XCT 3 -XCT2 3 -XCT3 4   -RGD 1 &
bash $R XMIX  $B -XCT 3 -XCT2 3 -XCT3 5   -RGD 1 -XCD 2 &
bash $R AE5   $B -XCT 4 -XCT2 4 -XCT3 4 -AT3 6 -AT32 6 -AT33 5 -RGD 1 &
bash $R ABT7  $B -XCT 4 -XCT2 4 -XCT3 4 -AT3 7 -AT32 7 -AT33 6 -RGD 1 &
wait
echo "[a14] BATCH 1 COMPLETE"
