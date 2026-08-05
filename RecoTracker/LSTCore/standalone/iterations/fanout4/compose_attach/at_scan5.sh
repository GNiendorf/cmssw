#!/bin/bash
# at_scan5.sh -- batch 5, final refinement.
# a8rps_rd is nearly a strict improvement (eff +.0002, fake 0.0000, dup -.0061, displaced
# flat except v1030 -.0016). Bracket it at 7.5 / 8.5, and probe the two remaining pieces
# of the M16 machinery that could add dup reach at the same point:
#   -RT3 1 -AT3 8  : turn ON stage B (bare-T3 attach) + wholesale pT3-class replacement
#   -D4 1.0        : the M7c dca eligibility guard, to see if blocking displaced chains
#                    from attaching protects the displaced slices that a7 gave up
cd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/compose_attach
run() { PROTO_ATTACH_CM=1 ./at_run.sh "$@" > "scan_$1.out" 2>&1; }
run a75rps_rd     -a 7.5 -RPS 1 -RD 1 &
run a85rps_rd     -a 8.5 -RPS 1 -RD 1 &
run a8rps_rd_rt3  -a 8 -RPS 1 -RD 1 -RT3 1 -AT3 8 &
run a8rps_rd_d4   -a 8 -RPS 1 -RD 1 -D4 1.0 &
wait
echo BATCH5DONE > scan5.done
