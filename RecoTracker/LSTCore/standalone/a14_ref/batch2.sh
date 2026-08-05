#!/bin/bash
# A14 batch 2 (launched alongside batch 1 to recover the lost hour). The SLOPE family is
# the simplicity hedge: one monotone shape constant s around the baseline -XCT 4, i.e.
# barrel 4-s / transition 4 / endcap 4+s, rather than three free numbers.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/a14_ref/a14_run.sh"
export BIN="$S/protoA14/bin/chainproto_v1"
B="-XC 3 -T3E 1 -CC 1 -CCN 1 -CCR 2"
go () { setsid nohup bash "$R" "$@" > /dev/null 2>&1 < /dev/null & }
go XE6    $B -XCT 4 -XCT2 4 -XCT3 6   -RGD 1
go XB25   $B -XCT 2.5 -XCT2 4 -XCT3 4 -RGD 1
go XSL1   $B -XCT 3 -XCT2 4 -XCT3 5   -RGD 1
go XSL2   $B -XCT 2 -XCT2 4 -XCT3 6   -RGD 1
go AB7E5  $B -XCT 4 -XCT2 4 -XCT3 4 -AT3 7 -AT32 7 -AT33 5 -RGD 1
go COMBO  $B -XCT 3 -XCT2 4 -XCT3 5 -AT3 7 -AT32 7 -AT33 5 -RGD 1
sleep 2
echo "[a14] BATCH 2 LAUNCHED (detached)"
