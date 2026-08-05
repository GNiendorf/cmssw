#!/bin/bash
# T3DEDUP-1 round 1: the dedup variant matrix, run in PARALLEL (physics is
# deterministic and independent of load; no timing claims are made from these).
cd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/t3attach_ref
B="-RT3 1 -CF 1 -CFC 1 -AT3 6"
run() { bash ta_run.sh "$@" > /dev/null 2>&1; echo "done $1"; }

run d_none   $B -RDT 0                        &   # no dedup at all
run d_ot     $B -RDT 0 -CC 1                  &   # (a) OT-only, MD gran, N=2
run d_both   $B -RDT 1 -CC 1                  &   # (b) pixel + OT
run d_px     $B -RDT 1                        &   # pixel side only
run d_n1     $B -RDT 1 -CC 1 -CCN 1           &   # granularity: any shared MD
run d_hit    $B -RDT 1 -CC 1 -CCG 0 -CCN 1    &   # hit granularity, strictest
run d_nopre  $B -RDT 1 -CC 1 -CCP 0           &   # staging: no chain pre-claim
run d_otn1   $B -RDT 0 -CC 1 -CCN 1           &   # OT-only, strictest
run d_k1     $B -RDT 1 -CC 1 -CCK 1           &   # keep-best by pLS pt
wait
echo ALLDONE_R1
