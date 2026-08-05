#!/bin/bash
# Batch 2: band-conditioned (-DDE) variants + the free length/order compensator (-L 3 -BT 4.5).
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/fanout5/ex_dupwin/ex_run.sh"
export BIN=local
bash $R e15t30 -DD 2 -DDK 1 -DDT 0.30 -DDE 1.5         &
bash $R e15t20 -DD 2 -DDK 1 -DDT 0.20 -DDE 1.5         &
bash $R e15t10 -DD 2 -DDK 1 -DDT 0.10 -DDE 1.5         &
bash $R lbt    -L 3.0 -BT 4.5                          &
wait
echo BATCH2_DONE
