#!/bin/bash
# Install a model's weights + WP tables into wt_cand and build CPU to bin/<name>.
# usage: build_arm.sh <name> <wp json> <creation r> <unused> <weights header>
T=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/displaced_ref/t5dnn
L=$T/wt_cand/RecoTracker/LSTCore
python3 $T/wt_train/RecoTracker/LSTCore/standalone/analysis/DNN/install_T5_WP.py --json $2 --creation $3 \
  --common-h $L/interface/alpaka/Common.h --weights-src $5 --weights-dst $L/src/alpaka/T5NeuralNetworkWeights.h || exit 2
$T/build_cand.sh $1
git -C $T/wt_cand diff > $T/bin/$1/cand.diff
echo "$1: json=$2 creation=$3 weights=$5 md5(weights)=$(md5sum < $5 | cut -c1-12)" > $T/bin/$1/ARM.txt
