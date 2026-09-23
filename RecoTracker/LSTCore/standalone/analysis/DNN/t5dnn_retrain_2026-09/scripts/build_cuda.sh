#!/bin/bash
# CUDA standalone build of a worktree: build_cuda.sh <worktree dir name under t5dnn> <bin name>  -> bin/<name>/{lst_cuda,liblst_cuda.so}
T=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/displaced_ref/t5dnn
S=$T/$1/RecoTracker/LSTCore/standalone
export SCRAM_ARCH=el9_amd64_gcc13
pushd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone >/dev/null
source setup.sh >/dev/null 2>&1; eval $(scramv1 runtime -sh 2>/dev/null); source setup.sh >/dev/null 2>&1
popd >/dev/null
cd $S || exit 2; source setup.sh >/dev/null 2>&1
./bin/lst_make_tracklooper -mG; LOG=$(ls -t $S/.make.log.* | head -1); echo "CUDA error_COUNT=$(grep -c 'error:' $LOG)"; grep 'error:' $LOG | head -5
mkdir -p $T/bin/$2 && cp -f bin/lst_cuda LST/liblst_cuda.so $T/bin/$2/ && md5sum $T/bin/$2/lst_cuda $T/bin/$2/liblst_cuda.so
echo "$2: $1 at $(git -C $T/$1 rev-parse --short=12 HEAD), dirty lines: $(git -C $T/$1 diff | wc -l)" > $T/bin/$2/ARM_cuda.txt
