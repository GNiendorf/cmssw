#!/bin/bash
# W5: build the weld-calibration instrument in gpu_wt/g3 (CPU only). No set -u.
G=/mnt/data1/gsn27/here/gpu_wt/g3/src/RecoTracker/LSTCore/standalone
O=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
cd $G || exit 9
source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) 2>/dev/null
source setup.sh > /dev/null 2>&1
lst_make_tracklooper -mcC > $O/w5_ref/build.log 2>&1
echo "make rc=$?" >> $O/w5_ref/build.log
LOG=$(ls -t $G/.make.log.* 2>/dev/null | head -1)
echo "fresh make log: $LOG"
echo "error: count = $(grep -c 'error:' $LOG)"
grep -n "error:" $LOG | head -20
grep -n "failed to compile" $LOG | head -5
md5sum $G/bin/lst_cpu $G/LST/liblst_cpu.so
