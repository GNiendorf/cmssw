#!/bin/bash
# A12 batch 3 -- final axes at the chosen margin. Args: $1 = chosen -a (default 5).
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
A="${1:-5}"
B="-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2 -XCD 2"
run() { bash $S/a12_ref/a12_run.sh "$@" ; }
run C_aT65  $B -a $A -AT3 6.5   &   # fake axis, milder than -AT3 7
run C_aX5   $B -a $A -XCT 5     &   # relax the crossclean arm (efficiency-leaning)
run C_a6X1  $B -a 6   -XC 1     &   # conservative margin + the simplicity variant
run C_aXC0  $B -a $A -XC 0      &   # ABLATION: no seed crossclean at all at the new margin
wait
echo "[a12] BATCH3 COMPLETE"
