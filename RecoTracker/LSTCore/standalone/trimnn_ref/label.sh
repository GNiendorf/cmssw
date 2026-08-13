#!/bin/bash
# label.sh <TAG> <dump.bin> <ntuple> <nent> <outdir> [--jets]
O=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
T=$O/trimnn_ref
TAG=$1; shift
cd $O && source setup.sh >/dev/null 2>&1 && eval $(scramv1 runtime -sh) >/dev/null 2>&1 && source setup.sh >/dev/null 2>&1
cd $O
python3 $T/py/truthv.py "$@" > $T/logs/label_$TAG.log 2>&1
echo "rc=$? $TAG" >> $T/logs/label.status
tail -4 $T/logs/label_$TAG.log
