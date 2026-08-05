#!/bin/bash
# A14 batch 5 -- REFINEMENT + 977 CONFIRMATION. Edit the go-lines before launching; this
# file is a template so the confirmation set can be fired the moment batch 4 lands.
#   usage: batch5.sh            (300-evt refinement)
#          W=1 batch5.sh        (977-evt confirmation of the chosen point)
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/a14_ref/a14_run.sh"
export BIN="$S/protoA14/bin/chainproto_v1"
B="-XC 3 -T3E 1 -CC 1 -CCN 1 -CCR 2"
go () { setsid nohup bash "$R" "$@" > /dev/null 2>&1 < /dev/null & }
if [ "${W:-0}" = "1" ]; then
  export LSTN="$S/rebase_ref/LSTNtuple_instr_977evt.root"
  export BASEHISTS="$S/fin_ref/fin_base977_hists.root"
  go W_WIN  $B $WINFLAGS
  go W_A0   $B -XCT 4
else
  go Q1 $B -XCT 4 -AT3 6 -AT33 7
  go Q2 $B -XCT 4 -AT3 6 -AT33 9
  go Q3 $B -XCT 4 -XCT3 3 -AT3 6 -AT33 9
  go Q4 $B -XCT 4.5 -XCT3 2.5 -AT3 6 -AT33 8
fi
sleep 2
echo "[a14] BATCH 5 LAUNCHED (W=${W:-0})"
