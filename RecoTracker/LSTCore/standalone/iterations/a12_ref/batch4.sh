#!/bin/bash
# A12 batch 4. Batch 3 produced the structural finding that reframes the whole round:
# `-a 5 -XCT 5` came out BIT-IDENTICAL to `-a 5 -XC 1` (the arm off) on all 14 metrics
# and on nTC. Reason: the pre-existing -RPS predicate already retires every seed whose
# best chain logit is >= -a, so the ported crossclean's bare-chain arm can only ever act
# in the BAND [-XCT, -a). Merge above -a, delete in [-XCT, -a), keep below -XCT.
#   D_a4X1     : the band closes at -a 4 -- the whole non-verbatim arm AND its tuned
#                constant -XCT should then be deletable. Proven at 12 evts (M7), this
#                proves it on the frozen 300.
#   D_a5T7X35  : dup-leaning version of the fake-leaning point.
#   D_a5T75    : one more step along the fake axis past -AT3 7 (fake 0.04954).
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
B="-T3E 1 -CC 1 -CCN 1 -CCR 2 -XCD 2"
run() { bash $S/a12_ref/a12_run.sh "$@" ; }
run D_a4X1     $B -XC 1            -a 4              &
run D_a5T7X35  $B -XC 3 -XCT 3.5   -a 5   -AT3 7     &
run D_a5T75    $B -XC 3 -XCT 4     -a 5   -AT3 7.5   &
wait
echo "[a12] BATCH4 COMPLETE"
