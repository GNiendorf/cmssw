#!/bin/bash
# run_q6c.sh <dir>: verify5's trackingMkFitFit CPU ttbar validation job (verify5_int/menu/runs/portfit_cpu_ttbar/hlt.py,
# area L/main = main 0a2bb8b in the vA area, OLD reference) rerun with the stock mkFit libraries switched to the release's
# x86-64-v2 build (LD_LIBRARY_PATH prefix r3_harness/floor500q/v2mkfit, as r5_menu's portv2 runs): the same-sample,
# same-job-shape distance port vs stock_v2, to compare with verify5's port vs stock_v3 (question 6c).
D=$1; L=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka
source /cvmfs/cms.cern.ch/cmsset_default.sh; export SCRAM_ARCH=el9_amd64_gcc13; export TMPDIR=$L/r6_menu/tmp
cd $L/main/CMSSW/src && eval $(scramv1 runtime -sh) && cd $D || exit 1
export LD_LIBRARY_PATH=$L/r3_harness/floor500q/v2mkfit:$LD_LIBRARY_PATH
echo "start $(date '+%-I:%M:%S %p') load $(cut -d' ' -f1 /proc/loadavg)" > times.txt
cmsRun hlt.py > hlt.log 2>&1 & tp=$!; echo $tp > cmsrun.pid
( for i in $(seq 1 120); do grep -q libRecoTrackerMkFitCore /proc/$tp/maps 2>/dev/null && { sleep 30; grep -oE '/[^ ]*(MkFit|mkfit)[^ ]*\.so' /proc/$tp/maps | sort -u > libs.txt; break; }; sleep 5; done ) &
wait $tp; rc=$?
echo "end $(date '+%-I:%M:%S %p') rc=$rc load $(cut -d' ' -f1 /proc/loadavg)" >> times.txt
