#!/bin/bash
# mkpatch.sh <TAG> <worktree> <baseChainConfig>
# Emits nnloop_ref/s2_work/s2_<TAG>.patch: the two changed files ONLY, diffed against the ARM'S
# OWN BASE (shipped for the control, shipped+armG for the candidate), so the patch applies on top
# of that base and nothing else.
O=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
W=$O/nnloop_ref/s2_work
TAG=$1; G=$2; BASECFG=$3
S=$G/src/RecoTracker/LSTCore
T=$W/.mk_$TAG; rm -rf $T; mkdir -p $T/base/RecoTracker/LSTCore/{interface,src/alpaka} $T/new/RecoTracker/LSTCore/{interface,src/alpaka}
cp $BASECFG                                     $T/base/RecoTracker/LSTCore/interface/ChainConfig.h
cp $W/base_shipped_Chain3NetworkWeights.h       $T/base/RecoTracker/LSTCore/src/alpaka/Chain3NetworkWeights.h
cp $S/interface/ChainConfig.h                   $T/new/RecoTracker/LSTCore/interface/ChainConfig.h
cp $S/src/alpaka/Chain3NetworkWeights.h         $T/new/RecoTracker/LSTCore/src/alpaka/Chain3NetworkWeights.h
cd $T && git diff --no-index --no-color base new | sed -e 's#^--- a/base/#--- a/#' -e 's#^+++ b/new/#+++ b/#' \
   -e 's#^diff --git a/base/#diff --git a/#' -e 's# b/new/# b/#' > $W/s2_$TAG.patch
cd $O; rm -rf $T
echo "wrote $W/s2_$TAG.patch ($(grep -c '^+' $W/s2_$TAG.patch) added lines, $(grep -c '^-' $W/s2_$TAG.patch) removed)"
