#!/bin/bash
# phase2_s3.sh <worktree> <TAG>
# The full gate set for an arm whose header + bars are ALREADY installed and built in <worktree>
# (i.e. run deploy_s3.sh first, which also does the PU200 tune run): the pooled 6000-event holdout
# (6 x 1000) plus both cubes. cube50_highPt drops -s 4 -> 2 -> 1 on the writer segfault, which
# scales with TC COUNT (S1 [22:52]).
O=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
W=$O/nnloop_ref/s3_work
G=$1; TAG=$2
V=/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3
S=$G/src/RecoTracker/LSTCore/standalone
cd $S && source setup.sh >/dev/null 2>&1 && eval $(scramv1 runtime -sh) >/dev/null 2>&1 && source setup.sh >/dev/null 2>&1
cd $S
R=$W/runs
run() { ./bin/lst_cpu "${@:2}" -o $R/$1.root > $R/$1.log 2>&1; echo "RUN_EXIT=$?" >> $R/$1.log
        python3 $O/d3_ref/pu_judge.py $R/$1.root --json $R/$1.json > $R/$1.judge 2>&1; }
for e in 2000 3000 4000 5000 6000 7000; do
  run ${TAG}_h$e -i $V/event_$e.root -n 1000 -s 8 -p 0.8 &
done
wait
echo "$TAG holdout done" >> $W/logs/phase2.status
run ${TAG}_cube50 -i cube50 -n 5000 -s 4 -p 0.8
for s in 4 2 1; do
  run ${TAG}_cubehi -i cube50_highPt -n 5000 -s $s -p 0.8
  if ! grep -q "RUN_EXIT=139" $R/${TAG}_cubehi.log; then echo "cubehi ok at -s $s" >> $W/logs/phase2.status; break; fi
  echo "cubehi SEGFAULT at -s $s, dropping" >> $W/logs/phase2.status
done
echo "$TAG PHASE2 DONE" >> $W/logs/phase2.status
