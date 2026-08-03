#!/bin/bash
set -x
cd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/t3attach_ref
bash ta_run.sh IDCF   -CF 1 -CFC 1
bash ta_run.sh s_a6   -RT3 1 -CF 1 -CFC 1 -AT3 6
bash ta_run.sh s_a6cc -RT3 1 -CF 1 -CFC 1 -AT3 6 -CC 1
bash ta_run.sh s_a8   -RT3 1 -CF 1 -CFC 1 -AT3 8
bash ta_run.sh s_a8cc -RT3 1 -CF 1 -CFC 1 -AT3 8 -CC 1
bash ta_run.sh s_a10  -RT3 1 -CF 1 -CFC 1 -AT3 10
bash ta_run.sh s_a4cc -RT3 1 -CF 1 -CFC 1 -AT3 4 -CC 1
bash ta_run.sh s_a6cc2 -RT3 1 -CF 1 -CFC 1 -AT3 6 -CC 1 -CCT 0.34
echo ALLDONE
