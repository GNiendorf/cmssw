#!/bin/bash
cd /mnt/data1/gsn27/here/gpu_wt/r2/src/RecoTracker/LSTCore/standalone || exit 9
source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) 2>/dev/null
source setup.sh > /dev/null 2>&1
lst_make_tracklooper -mcC > /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/trim_ref/build.log 2>&1
echo "rc=$?"
LOG=$(ls -t .make.log.* 2>/dev/null | head -1)
echo "fresh log: $LOG   error: $(grep -c 'error:' $LOG)   failed-to-compile: $(grep -c 'failed to compile' $LOG)"
grep 'error:' $LOG | head -10
md5sum bin/lst_cpu LST/liblst_cpu.so 2>/dev/null
