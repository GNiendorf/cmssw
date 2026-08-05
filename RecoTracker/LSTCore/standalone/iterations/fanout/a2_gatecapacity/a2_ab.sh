#!/bin/bash
# a2 A/B runner: chainproto hybrid -> createPerfNumDenHists -> compare_ab.py
# usage: ./a2_ab.sh <tag> <config flags...>
set -e
D=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P=$D/prototype
M=$D/fanout/a2_gatecapacity
TAG=$1; shift
$M/bin/chainproto -i $D/LSTNtuple_PU200RelVal_300evt.root \
  -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
  -m hybrid "$@" -o $M/ab_$TAG.root > $M/ab_$TAG.log 2>&1
createPerfNumDenHists -i $M/ab_$TAG.root -o $M/ab_${TAG}_hists.root > $M/ab_${TAG}_hist.log 2>&1
python3 $P/compare_ab.py --proto $M/ab_${TAG}_hists.root --base $P/base300_hists.root \
  --json $M/ab_$TAG.json > $M/ab_${TAG}_cmp.log 2>&1
rm -f $M/ab_$TAG.root $M/ab_${TAG}_hists.root
echo "=== $TAG : $* ==="
cat $M/ab_${TAG}_cmp.log
