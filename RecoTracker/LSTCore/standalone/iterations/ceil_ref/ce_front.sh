#!/bin/bash
# The CONDITIONED frontier: does any conditioned operating point reach the gate?
# e_n1 (-AT3 6) came in at eff .80873 / dup .06176 / fake .06010 -- efficiency at the top
# of the r2-head family but both other gates missed. Tighten the margin and the
# conditioning and see whether the frontier ever crosses.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
BASE="-RT3 1 -CF 1 -CFC 1 -RDT 1 -CC 1 -CCG 1 -CCP 1 -CCR 2 -CCN 1"
bash $S/ceil_ref/ce_run.sh e_n1_a75 $BASE -AT3 7.5 -CE 1 -CEN 1        > $S/ceil_ref/e_n1_a75.nohup 2>&1 &
bash $S/ceil_ref/ce_run.sh e_n1_a9  $BASE -AT3 9   -CE 1 -CEN 1        > $S/ceil_ref/e_n1_a9.nohup  2>&1 &
bash $S/ceil_ref/ce_run.sh e_n1L    $BASE -AT3 6   -CE 1 -CEN 1 -CEL 1 > $S/ceil_ref/e_n1L.nohup    2>&1 &
wait
echo FRONTDONE
