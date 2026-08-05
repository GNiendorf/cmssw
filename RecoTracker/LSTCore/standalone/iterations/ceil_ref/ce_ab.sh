#!/bin/bash
# THE DECISIVE C++ A/B: post-hoc veto vs the SAME rule applied at ENUMERATION.
# Everything else is A's finalist dedup stack (-RDT 1 -CC 1 -CCG 1 -CCN n -CCP 1 -CCR 2)
# on the frozen M19/P2.5 flagship line, r2 head (protoCEIL carries no dedicated T3 head,
# so the control is the r2-head family: M20 d_n1 / GEN-A g_* are the reference points).
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
BASE="-RT3 1 -CF 1 -CFC 1 -RDT 1 -CC 1 -CCG 1 -CCP 1 -CCR 2"
bash $S/ceil_ref/ce_run.sh p_n1  $BASE -CCN 1 -AT3 6            > $S/ceil_ref/p_n1.nohup  2>&1 &
bash $S/ceil_ref/ce_run.sh e_n1  $BASE -CCN 1 -AT3 6 -CE 1 -CEN 1 > $S/ceil_ref/e_n1.nohup 2>&1 &
bash $S/ceil_ref/ce_run.sh p_n2  $BASE -CCN 2 -AT3 6            > $S/ceil_ref/p_n2.nohup  2>&1 &
bash $S/ceil_ref/ce_run.sh e_n2  $BASE -CCN 2 -AT3 6 -CE 1 -CEN 2 > $S/ceil_ref/e_n2.nohup 2>&1 &
wait
echo ABDONE
