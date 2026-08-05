#!/bin/bash
# scanN.sh "tag:overrides" ...   -- runs all in parallel against the FLAGSHIP base.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
D=$S/fanout5/ex_extend
B=$D/bin/chainproto
for spec in "$@"; do
  tag="${spec%%:*}"; ov="${spec#*:}"
  bash $D/run.sh $B "$tag" $ov &
done
wait
echo ALLDONE
