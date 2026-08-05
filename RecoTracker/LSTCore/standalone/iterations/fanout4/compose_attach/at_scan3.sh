#!/bin/bash
# at_scan3.sh -- batch 3, refinement around the -RPS knee.
# Batch 2 result: -RPS 1 is the only lever with real dup reach (it retires bare-pLS
# type-8 rows, which plain contention cannot because -RT5 1 already removed the type-7
# rows the attached pLS mostly belonged to). a7rps passed every floor at dup -.0079;
# a4rps failed v510/v1030/d510 by <.001. So the floor edge sits between -a 4 and -a 7:
# scan 5/6/8/10, plus the -RD (seed-family dedup of attach owners) variant at the knee.
cd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/compose_attach
run() { PROTO_ATTACH_CM=1 ./at_run.sh "$@" > "scan_$1.out" 2>&1; }
for A in 5 6 8 10; do run a${A}rps -a $A -RPS 1 & done
run a7rps_rd -a 7 -RPS 1 -RD 1 &
run a6 -a 6 &
wait
echo BATCH3DONE > scan3.done
