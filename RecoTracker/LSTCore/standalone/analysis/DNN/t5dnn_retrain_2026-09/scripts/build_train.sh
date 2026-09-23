#!/bin/bash
# T5 DNN training build: worktree wt_train (master + segment counting + t5x_* features, T5 DNN creation cut off), CPU, -d.
T=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/displaced_ref/t5dnn
S=$T/wt_train/RecoTracker/LSTCore/standalone
export SCRAM_ARCH=el9_amd64_gcc13
pushd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone >/dev/null
source setup.sh >/dev/null 2>&1; eval $(scramv1 runtime -sh 2>/dev/null); source setup.sh >/dev/null 2>&1
popd >/dev/null
cd $S || exit 2; source setup.sh >/dev/null 2>&1
echo "TRACKLOOPERDIR=$TRACKLOOPERDIR"; date
./bin/lst_make_tracklooper -mCd; LOG=$(ls -t $S/.make.log.* | head -1); echo "CPU error_COUNT=$(grep -c 'error:' $LOG)"; grep 'error:' $LOG | head -20
mkdir -p $T/bin/train && cp -f bin/lst_cpu LST/liblst_cpu.so $T/bin/train/ && md5sum $T/bin/train/*
git -C $T/wt_train rev-parse HEAD; git -C $T/wt_train diff --stat | tail -1
date; echo "BUILD DONE"
