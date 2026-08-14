#!/bin/bash
# W5: cube50 retry at an alternative stream count (the known unpredictable writer segfault).
#   usage: cube_retry.sh <TAG> <streams> <BINDIR>
TAG=$1; S=${2:-8}; A=$3
O=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
G=/mnt/data1/gsn27/here/gpu_wt/g3/src/RecoTracker/LSTCore/standalone
R=$O/w5_ref/runs
cd $G && source setup.sh >/dev/null 2>&1 && eval $(scramv1 runtime -sh) >/dev/null 2>&1 && source setup.sh >/dev/null 2>&1
cd $G
export LD_LIBRARY_PATH=$A/LST:$LD_LIBRARY_PATH
rm -f $R/${TAG}s${S}_cube50.root
$A/bin/lst_cpu -i cube50 -n -1 -s $S -p 0.8 -w 1 -o $R/${TAG}s${S}_cube50.root > $R/${TAG}s${S}_cube50.log 2>&1
echo "RUN_EXIT=$?" >> $R/${TAG}s${S}_cube50.log
python3 $O/d3_ref/pu_judge.py $R/${TAG}s${S}_cube50.root --json $R/${TAG}s${S}_cube50.json > $R/${TAG}s${S}_cube50.judge 2>&1
echo "$TAG s$S cube50 done"
