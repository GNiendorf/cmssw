#!/bin/bash
# paired_s3.sh <TAG> <ship|gm12f> -- pooled 6000-event McNemar vs one of the TWO baselines.
#   ship   baseline = the CURRENT SHIPPED ALGORITHM (holdout_ref/Chains.root + a5_ref/big/ours_*)
#   gm12f  baseline = the GM12F pipeline (S2's own holdout runs) -- isolates stage 3's effect
O=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
W=$O/nnloop_ref/s3_work
TAG=$1; WHICH=$2
if [ "$WHICH" = "ship" ]; then
  REF=$O/holdout_ref/Chains.root
  for e in 3000 4000 5000 6000 7000; do REF=$REF,$O/a5_ref/big/ours_$e.root; done
  RN=SHIPPED4NN
else
  REF=$O/nnloop_ref/s2_work/runs/GM12F_h2000.root
  for e in 3000 4000 5000 6000 7000; do REF=$REF,$O/nnloop_ref/s2_work/runs/GM12F_h$e.root; done
  RN=GM12F
fi
ARM=$W/runs/${TAG}_h2000.root
for e in 3000 4000 5000 6000 7000; do ARM=$ARM,$W/runs/${TAG}_h$e.root; done
cd $O && python3 a5_ref/paired.py "$REF" "$ARM" $RN $TAG
