#!/bin/bash
cd /mnt/data1/gsn27/here/gpu_wt/r2b/src/RecoTracker/LSTCore/standalone || exit 9
source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) 2>/dev/null
source setup.sh > /dev/null 2>&1
TS=$(date +%s)
lst_make_tracklooper -mcC > /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/union_ref/build.log 2>&1
echo "rc=$?"
LOG=$(ls -t .make.log.* 2>/dev/null | head -1)
echo "fresh log: $LOG"
echo "error: count = $(grep -c 'error:' $LOG)"
echo "failed-to-compile = $(grep -c 'failed to compile' $LOG)"
grep 'error:' $LOG | head -20
md5sum bin/lst_cpu LST/liblst_cpu.so 2>/dev/null
