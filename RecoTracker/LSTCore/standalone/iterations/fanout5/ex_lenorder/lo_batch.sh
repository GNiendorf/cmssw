#!/bin/bash
# lo_batch.sh <maxpar> "<tag>|<overrides>" ...   -- FLAGSHIP base for every entry.
D=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/ex_lenorder
P=$1; shift
i=0
for spec in "$@"; do
  tag=${spec%%|*}
  ovr=${spec#*|}
  bash $D/lo_run.sh $tag flag $ovr &
  i=$((i+1))
  if [ $((i % P)) -eq 0 ]; then wait; fi
done
wait
echo "BATCH COMPLETE"
