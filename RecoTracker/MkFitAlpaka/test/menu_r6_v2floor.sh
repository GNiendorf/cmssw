#!/bin/bash
# run_v2.sh <dir> <area> <v2 libdir>: rerun <dir>/hlt.py in <area> with the stock mkFit libraries switched to x86-64-v2
# builds (LD_LIBRARY_PATH prefix): the same-sample in-menu proxy floor (port vs stock_v2) of a validation job.
D=$1; AREA=$2; V2=$3; L=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka
source /cvmfs/cms.cern.ch/cmsset_default.sh; export SCRAM_ARCH=el9_amd64_gcc13; export TMPDIR=$L/r6_menu/tmp
cd $AREA/src && eval $(scramv1 runtime -sh) && cd $D || exit 1
export LD_LIBRARY_PATH=$V2:$LD_LIBRARY_PATH
echo "start $(date '+%-I:%M:%S %p') load $(cut -d' ' -f1 /proc/loadavg)" > times.txt
cmsRun hlt.py > hlt.log 2>&1 & tp=$!; echo $tp > cmsrun.pid
( for i in $(seq 1 120); do grep -q libRecoTrackerMkFitCore /proc/$tp/maps 2>/dev/null && { sleep 30; grep -oE '/[^ ]*(MkFit|mkfit)[^ ]*\.so' /proc/$tp/maps | sort -u > libs.txt; break; }; sleep 5; done ) &
wait $tp; rc=$?
echo "end $(date '+%-I:%M:%S %p') rc=$rc load $(cut -d' ' -f1 /proc/loadavg)" >> times.txt
