#!/bin/bash
# T5 DNN candidate build: worktree wt_cand (3-class T5 DNN), CPU (arg: bin subdir name).
T=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/displaced_ref/t5dnn
S=$T/wt_cand/RecoTracker/LSTCore/standalone
export SCRAM_ARCH=el9_amd64_gcc13
pushd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone >/dev/null
source setup.sh >/dev/null 2>&1; eval $(scramv1 runtime -sh 2>/dev/null); source setup.sh >/dev/null 2>&1
popd >/dev/null
cd $S || exit 2; source setup.sh >/dev/null 2>&1
echo "TRACKLOOPERDIR=$TRACKLOOPERDIR"; date
./bin/lst_make_tracklooper -mCd; LOG=$(ls -t $S/.make.log.* | head -1); echo "CPU error_COUNT=$(grep -c 'error:' $LOG)"; grep 'error:' $LOG | head -20

git -C $T/wt_cand rev-parse HEAD; git -C $T/wt_cand diff --stat | tail -1
date; echo "BUILD DONE"
