#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
export BIN=$S/protoA03/bin/chainproto_v2
export LSTN=$S/rebase_ref/LSTNtuple_instr_977evt.root
export BASEHISTS=$S/fin_ref/fin_base977_hists.root
bash $S/a03_ref/a03_run.sh W977_S15C35 -ATS 1.5 -XCT 3.5
echo "BATCH977 DONE"
