#!/bin/bash
# at_scan1.sh -- ATTACH-CONTENTION scan, batch 1.
# -a points chosen from the a999 best-per-target frontier (see at_a999.log [ATTACHCM]):
#   a=0  924/evt attachable, head precision .852   (loose)
#   a=4  831/evt,            precision .933
#   a=7  644/evt,            precision .962
#   a=9  280/evt,            precision .968        (precision saturates here)
# Suppression is inherent at -A 4 (an owned pLS's carried type-5/8 rows retire); -RPS
# variants are batch 2.
cd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/compose_attach
for A in 0 4 7 9; do
  PROTO_ATTACH_CM=1 ./at_run.sh a$A -a $A > scan_a$A.out 2>&1 &
done
wait
echo BATCH1DONE > scan1.done
