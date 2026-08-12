#!/bin/bash
# S1: pooled 6000-event paired (McNemar) holdout, shipped vs arm.
# The writer emits events in stream-completion order, so a5_ref/paired.py sorts on sim content.
O=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R=$O/nnloop_ref/s1_work/runs
P=${1:-S1R1}
SHIP=$O/holdout_ref/Chains.root
ARM=$R/${P}_h2000.root
for e in 3000 4000 5000 6000 7000; do
  SHIP=$SHIP,$O/a5_ref/big/ours_$e.root
  ARM=$ARM,$R/${P}_h$e.root
done
cd $O && python3 a5_ref/paired.py "$SHIP" "$ARM" SHIPPED $P
