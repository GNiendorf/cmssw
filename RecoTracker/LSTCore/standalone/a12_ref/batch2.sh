#!/bin/bash
# A12 batch 2 -- refine around -a 5 (the batch-1 winner) and test what it makes redundant.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
B="-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2 -XCD 2"
run() { bash $S/a12_ref/a12_run.sh "$@" ; }
run B_a45   $B -a 4.5        &   # refine the margin
run B_a55   $B -a 5.5        &   # refine the margin
run B_a5X1  $B -a 5 -XC 1    &   # SIMPLICITY: is the bare-chain arm (and -XCT) still needed?
run B_a5X2  $B -a 5 -XCT 2   &   # hand the sub-margin residue back to the crossclean arm
run B_a5T7  $B -a 5 -AT3 7   &   # the fake knob at the new operating point
wait
echo "[a12] BATCH2 COMPLETE"
