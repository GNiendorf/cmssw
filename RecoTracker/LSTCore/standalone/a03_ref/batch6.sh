#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
export BIN=$S/protoA03/bin/chainproto_v2
bash $S/a03_ref/a03_run.sh XS20C375 -ATS 2.0 -XCT 3.75 &
bash $S/a03_ref/a03_run.sh XS10C375 -ATS 1.0 -XCT 3.75 &
wait
echo "BATCH6 DONE"
