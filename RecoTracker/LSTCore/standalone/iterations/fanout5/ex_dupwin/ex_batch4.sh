#!/bin/bash
# Batch 4: parity re-check on the final binary + the compensated frontier and the
# two-tier (-DDT2) barrel extension.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/fanout5/ex_dupwin/ex_run.sh"
export BIN=local
D="-DD 2 -DDK 1"
bash $R pfl3                                                                &
bash $R lbt_e15t20    -L 3.0 -BT 4.5 $D -DDT 0.20 -DDE 1.5                  &
bash $R lbt_e15t10    -L 3.0 -BT 4.5 $D -DDT 0.10 -DDE 1.5                  &
bash $R lbt_e15t30d1  -L 3.0 -BT 4.5 $D -DDT 0.30 -DDE 1.5 -DDD 1.0         &
bash $R lbt_e15t30b03 -L 3.0 -BT 4.5 $D -DDT 0.30 -DDE 1.5 -DDT2 0.03       &
bash $R lbtb03d1      -L 3.0 -BT 4.5 $D -DDT 0.30 -DDE 1.5 -DDT2 0.03 -DDD 1.0 &
bash $R l3_e15t30     -L 3.0         $D -DDT 0.30 -DDE 1.5                  &
bash $R lbt_e15t30k0  -L 3.0 -BT 4.5 -DD 2 -DDK 0 -DDT 0.30 -DDE 1.5        &
wait
echo BATCH4_DONE
