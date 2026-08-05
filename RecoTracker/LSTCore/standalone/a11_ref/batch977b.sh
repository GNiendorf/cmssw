#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/a11_ref/a11_run.sh"
export LSTN="$S/rebase_ref/LSTNtuple_instr_977evt.root"
export BASEHISTS="$S/fin_ref/fin_base977_hists.root"
bash "$R" W_C_A60 -XCD 2 -T3F 0.10 -RPSA 6.875 -RPST 6 -a 6.0 &
wait
echo "[a11] 977 BATCH B COMPLETE"
