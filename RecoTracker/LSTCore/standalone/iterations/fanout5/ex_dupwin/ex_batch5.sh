#!/bin/bash
# Batch 5: final binary parity + margin-buyback (-DDD) and one intermediate ptr point.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/fanout5/ex_dupwin/ex_run.sh"
export BIN=local
D="-DD 2 -DDK 1"
bash $R pfl4                                                        &
bash $R lbt_e15t15   -L 3.0 -BT 4.5 $D -DDT 0.15 -DDE 1.5           &
bash $R lbt_e15t20d5 -L 3.0 -BT 4.5 $D -DDT 0.20 -DDE 1.5 -DDD 5.0  &
bash $R lbt_e15t20d2 -L 3.0 -BT 4.5 $D -DDT 0.20 -DDE 1.5 -DDD 2.0  &
wait
echo BATCH5_DONE
