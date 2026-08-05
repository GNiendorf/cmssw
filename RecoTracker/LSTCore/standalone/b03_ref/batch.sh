#!/bin/bash
# B03 batch runner. usage: batch.sh "TAG:overrides" "TAG:overrides" ...
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
CF="-T3F 0.10 -XC4 1 -RPSA 5.5 -EXR 4.0 -a 6.0"
for spec in "$@"; do
  TAG="${spec%%:*}"; OV="${spec#*:}"
  ( BIN=$S/protoB03/bin/chainproto NEV="${NEV:--1}" LSTN="${LSTN:-$S/rebase_ref/LSTNtuple_instr_300evt.root}" \
    BASEHISTS="${BASEHISTS:-$S/rebase_ref/rb_base300_hists.root}" \
    bash $S/synth_ref/syn_run.sh "$TAG" $CF $OV > $S/b03_ref/run_$TAG.out 2>&1 ) &
done
wait
echo "BATCH DONE"
