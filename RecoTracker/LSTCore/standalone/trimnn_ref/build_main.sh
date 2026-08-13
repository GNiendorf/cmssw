#!/bin/bash
cd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone || exit 9
source setup.sh > /dev/null 2>&1; eval $(scramv1 runtime -sh) 2>/dev/null; source setup.sh > /dev/null 2>&1
lst_make_tracklooper -mcCG > trimnn_ref/build_main.log 2>&1
echo "rc=$?"
LOG=$(ls -t .make.log.* 2>/dev/null | head -1)
echo "log $LOG  error: $(grep -c 'error:' $LOG)  failed: $(grep -c 'failed to compile' $LOG)"
grep 'error:' $LOG | head -8
md5sum bin/lst_cpu bin/lst_cuda 2>/dev/null
