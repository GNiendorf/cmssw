#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
V=$S/verify1_ref
BASE="-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2"

BIN=$S/protoA01b/bin/chainproto nohup bash $V/v1_run.sh VA01_GATE $BASE > $V/b_VA01_GATE.out 2>&1 &
BIN=$S/protoA01b/bin/chainproto nohup bash $V/v1_run.sh VA01_BEST $BASE -XCO 5 > $V/b_VA01_BEST.out 2>&1 &
BIN=$S/protoA11/bin/chainproto nohup bash $V/v1_run.sh VA11_GATE $BASE > $V/b_VA11_GATE.out 2>&1 &
BIN=$S/protoA11/bin/chainproto nohup bash $V/v1_run.sh VA11_BEST $BASE -T3F 0.10 -RPSA 5.0 -RPST 6 -a 6.0 > $V/b_VA11_BEST.out 2>&1 &
BIN=$S/protoA06/bin/chainproto nohup bash $V/v1_run.sh VA06_GATE $BASE > $V/b_VA06_GATE.out 2>&1 &
BIN=$S/protoA06/bin/chainproto nohup bash $V/v1_run.sh VA06_BEST $BASE -EXW 0.50 -EXR 4.0 -L 5.0 > $V/b_VA06_BEST.out 2>&1 &
wait
echo ALL_LAUNCH_DONE
