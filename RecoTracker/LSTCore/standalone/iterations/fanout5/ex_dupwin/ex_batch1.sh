#!/bin/bash
# Batch 1: local-binary parity + the -DDT (pt-consistency) scan on the FLAGSHIP stack.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/fanout5/ex_dupwin/ex_run.sh"
export BIN=local
bash $R pfl                                            &
bash $R t03  -DD 2 -DDK 1 -DDT 0.03                    &
bash $R t05  -DD 2 -DDK 1 -DDT 0.05                    &
bash $R t07  -DD 2 -DDK 1 -DDT 0.07                    &
bash $R t10  -DD 2 -DDK 1 -DDT 0.10                    &
bash $R t15  -DD 2 -DDK 1 -DDT 0.15                    &
bash $R t20  -DD 2 -DDK 1 -DDT 0.20                    &
wait
echo BATCH1_DONE
