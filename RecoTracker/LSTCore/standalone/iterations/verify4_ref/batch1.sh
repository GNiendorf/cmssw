#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="bash $S/verify4_ref/v4_run.sh"
BASE="-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2"
# --- A04: gate (unedited copied binary) and the recommended -a 6
BIN=$S/a04_ref/chainproto_base       $R V4_A04_GATE  $BASE            &
BIN=$S/a04_ref/chainproto_base       $R V4_A04_A6    $BASE -a 6       &
# --- A09: gate on the EDITED binary at defaults, WITHOUT -XCD (tests -XCD inertness too)
BIN=$S/protoA09/bin/chainproto_a09   $R V4_A09_GATE  $BASE            &
BIN=$S/protoA09/bin/chainproto_a09   $R V4_A09_BEST  $BASE -RPS 3 -AT3 7 -XCD 2 &
BIN=$S/protoA09/bin/chainproto_a09   $R V4_A09_BESTNX $BASE -RPS 3 -AT3 7 &
# --- A14: gate on the EDITED binary at defaults == its recommended config
BIN=$S/protoA14/bin/chainproto_v1    $R V4_A14_GATE  $BASE            &
wait
echo "BATCH1 COMPLETE"
