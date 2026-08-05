#!/bin/bash
# Grid A: the mandated -TT x -TA scan on otherwise-flagship flags.
# TT in {1.0,1.1,1.2,1.5,2.0,3.0} x TA in {0.5,1.0,3.0}. 18 points, 3 batches of 6.
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/ex_trim
R=$P/xt_run.sh

for TA in 0.5 1.0 3.0; do
  TAG_TA=$(echo $TA | tr -d '.')
  for TT in 1.0 1.1 1.2 1.5 2.0 3.0; do
    TAG_TT=$(echo $TT | tr -d '.')
    "$R" a_tt${TAG_TT}_ta${TAG_TA} -TT $TT -TA $TA > "$P/drv_a_tt${TAG_TT}_ta${TAG_TA}.out" 2>&1 &
  done
  wait
  echo "GRIDA TA=$TA DONE"
done
touch $P/gridA.done
echo "GRIDA ALL DONE"
