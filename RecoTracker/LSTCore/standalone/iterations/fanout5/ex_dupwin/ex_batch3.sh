#!/bin/bash
# Batch 3: band ptr ceiling, the displaced exemption -DDD, and the length compensators.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/fanout5/ex_dupwin/ex_run.sh"
export BIN=local
bash $R e15t50     -DD 2 -DDK 1 -DDT 0.50 -DDE 1.5                  &
bash $R e15t99     -DD 2 -DDK 1 -DDE 1.5                            &
bash $R e15t30d05  -DD 2 -DDK 1 -DDT 0.30 -DDE 1.5 -DDD 0.5         &
bash $R d05t10     -DD 2 -DDK 1 -DDT 0.10 -DDD 0.5                  &
bash $R d05t20     -DD 2 -DDK 1 -DDT 0.20 -DDD 0.5                  &
bash $R lbt_e15t30 -L 3.0 -BT 4.5 -DD 2 -DDK 1 -DDT 0.30 -DDE 1.5   &
bash $R lbt_e15t50 -L 3.0 -BT 4.5 -DD 2 -DDK 1 -DDT 0.50 -DDE 1.5   &
bash $R bt4_e15t30 -BT 4 -DD 2 -DDK 1 -DDT 0.30 -DDE 1.5            &
wait
echo BATCH3_DONE
