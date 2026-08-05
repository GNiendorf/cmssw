#!/bin/bash
# at_scan4.sh -- batch 4. -RD (seed-family dedup of attach owners) across the knee.
# a7rps_rd beat a7rps on BOTH eff (+.0001) and fake (-.0004) at identical dup, so -RD
# looks free; check whether it also rescues the looser points, whose displaced slices
# were the part that regressed.
cd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/compose_attach
run() { PROTO_ATTACH_CM=1 ./at_run.sh "$@" > "scan_$1.out" 2>&1; }
for A in 5 6 8 9; do run a${A}rps_rd -a $A -RPS 1 -RD 1 & done
wait
echo BATCH4DONE > scan4.done
