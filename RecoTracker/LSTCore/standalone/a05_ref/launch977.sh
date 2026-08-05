#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
export LSTN="$S/rebase_ref/LSTNtuple_instr_977evt.root"
export BASEHISTS="$S/fin_ref/fin_base977_hists.root"
bash "$S/a05_ref/a05_run.sh" W_F10 -XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2 -AT3F 0.1
