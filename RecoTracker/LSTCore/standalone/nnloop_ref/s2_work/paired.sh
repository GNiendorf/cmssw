#!/bin/bash
# paired.sh <TAG> <ctl|cand> -- pooled 6000-event McNemar vs the ARM'S OWN baseline.
#   ctl  baseline = the SHIPPED heads (holdout_ref/Chains.root + a5_ref/big/ours_*)
#   cand baseline = S1's arm-G runs (nnloop_ref/s1_work/runs/S1G_h*), i.e. arm G edge + SHIPPED gate
O=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
W=$O/nnloop_ref/s2_work
TAG=$1; WHICH=$2
if [ "$WHICH" = "ctl" ]; then
  REF=$O/holdout_ref/Chains.root
  for e in 3000 4000 5000 6000 7000; do REF=$REF,$O/a5_ref/big/ours_$e.root; done
  RN=SHIPPED
else
  REF=$O/nnloop_ref/s1_work/runs/S1G_h2000.root
  for e in 3000 4000 5000 6000 7000; do REF=$REF,$O/nnloop_ref/s1_work/runs/S1G_h$e.root; done
  RN=ARMG_SHIPGATE
fi
ARM=$W/runs/${TAG}_h2000.root
for e in 3000 4000 5000 6000 7000; do ARM=$ARM,$W/runs/${TAG}_h$e.root; done
cd $O && python3 a5_ref/paired.py "$REF" "$ARM" $RN $TAG
