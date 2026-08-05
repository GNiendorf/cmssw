#!/bin/bash
# Frozen test-60 scoring for a list of tags (protocol copied verbatim from
# fanout3/m16/run_t60.sh: filter ds_<tag>.root to prototype/m12_test60_evts.txt,
# re-histogram, judge vs prototype/base60_hists.root).
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/descent
cd "$P" || exit 1
for t in "$@"; do ./ds_t60.sh "$t" & done
wait
echo DS_T60_DONE "$@"
