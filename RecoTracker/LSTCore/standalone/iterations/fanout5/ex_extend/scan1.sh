#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
D=$S/fanout5/ex_extend
B=$D/bin/chainproto
# Window scan, OUTER end only (-EX 1). 6 points.
bash $D/run.sh $B ex_w010 -EX 1 -EXW 0.10 &
bash $D/run.sh $B ex_w025 -EX 1 -EXW 0.25 &
bash $D/run.sh $B ex_w050 -EX 1 -EXW 0.50 &
bash $D/run.sh $B ex_w100 -EX 1 -EXW 1.00 &
bash $D/run.sh $B ex_w200 -EX 1 -EXW 2.00 &
bash $D/run.sh $B ex_w400 -EX 1 -EXW 4.00 &
wait
echo ALLDONE
