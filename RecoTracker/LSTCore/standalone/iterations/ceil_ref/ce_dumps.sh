#!/bin/bash
# Five 300-evt audit dumps: the UNCONDITIONED universe and the four conditioned ones.
# All at -AT3 -1e9 -RDT 0 -CC 0 so ONE dump carries the whole threshold curve and every
# dedup rule is replayed offline (the GEN-A methodology, verbatim).
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
COMMON="-RT3 1 -CF 1 -CFC 1 -AT3 -1e9 -RDT 0 -CC 0"
bash $S/ceil_ref/ce_run.sh U0 $COMMON              -QD $S/ceil_ref/aud_U0.txt > $S/ceil_ref/U0.nohup 2>&1 &
bash $S/ceil_ref/ce_run.sh C1 $COMMON -CE 1 -CEN 1 -QD $S/ceil_ref/aud_C1.txt > $S/ceil_ref/C1.nohup 2>&1 &
bash $S/ceil_ref/ce_run.sh C2 $COMMON -CE 1 -CEN 2 -QD $S/ceil_ref/aud_C2.txt > $S/ceil_ref/C2.nohup 2>&1 &
bash $S/ceil_ref/ce_run.sh C3 $COMMON -CE 1 -CEN 3 -QD $S/ceil_ref/aud_C3.txt > $S/ceil_ref/C3.nohup 2>&1 &
bash $S/ceil_ref/ce_run.sh CL $COMMON -CE 1 -CEN 9 -CEL 1 -QD $S/ceil_ref/aud_CL.txt > $S/ceil_ref/CL.nohup 2>&1 &
wait
echo ALLDONE
