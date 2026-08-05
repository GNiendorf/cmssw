#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
export BIN=$S/protoA03/bin/chainproto_v2
bash $S/a03_ref/a03_run.sh XS15C35 -ATS 1.5 -XCT 3.5 &
bash $S/a03_ref/a03_run.sh XS20C35 -ATS 2.0 -XCT 3.5 &
bash $S/a03_ref/a03_run.sh XS30C30 -ATS 3.0 -XCT 3.0 &
bash $S/a03_ref/a03_run.sh XS20C30 -ATS 2.0 -XCT 3.0 &
wait
echo "BATCH5 DONE"
