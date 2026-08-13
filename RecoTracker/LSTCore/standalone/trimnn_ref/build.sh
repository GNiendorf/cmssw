#!/bin/bash
# build.sh [flags]  -- default -mcC (clean CPU). Greps the FRESH make log for real errors.
A=/mnt/data1/gsn27/here/gpu_wt/tn1/src/RecoTracker/LSTCore/standalone
O=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/trimnn_ref
FL=${1:--mcC}
cd $A || exit 9
source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) 2>/dev/null
source setup.sh > /dev/null 2>&1
lst_make_tracklooper $FL > $O/logs/build.log 2>&1
echo "rc=$?"
LOG=$(ls -t .make.log.* 2>/dev/null | head -1)
echo "fresh log: $LOG   error: $(grep -c 'error:' $LOG)   failed-to-compile: $(grep -c 'failed to compile' $LOG)"
grep -n 'error:' $LOG | head -20
grep -n 'failed to compile' $LOG | head -5
md5sum bin/lst_cpu LST/liblst_cpu.so 2>/dev/null
