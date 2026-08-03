#!/bin/bash
cd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/t3attach_ref
B="-RT3 1 -CF 1 -CFC 1 -AT3 6"
NEV=30 bash ta_run.sh NOOP2                                    # no-op gate vs frozen
bash ta_run.sh d_none   $B -RDT 0                              # no dedup at all
bash ta_run.sh d_ot     $B -RDT 0 -CC 1                        # (a) OT-only, MD gran, N=2
bash ta_run.sh d_both   $B -RDT 1 -CC 1                        # (b) pixel + OT
bash ta_run.sh d_px     $B -RDT 1                              # pixel side only
bash ta_run.sh d_n1     $B -RDT 1 -CC 1 -CCN 1                 # granularity: any shared MD
bash ta_run.sh d_hit    $B -RDT 1 -CC 1 -CCG 0 -CCN 1          # hit granularity, strictest
bash ta_run.sh d_nopre  $B -RDT 1 -CC 1 -CCP 0                 # staging: no chain pre-claim
echo ALLDONE
