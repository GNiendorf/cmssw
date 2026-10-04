#!/bin/bash
# bench.sh <arm stock|port> <cfg> <outdir> [jobs threads streams events skip]: patatrack benchmark (= hltqcd/timing/bench.sh)
A=$1; CFG=$2; O=$3; J=${4:-2}; TH=${5:-8}; ST=${6:-8}; EV=${7:-300}; SK=${8:-50}; T=/mnt/data1/gsn27/here/hltqcd/timing
R=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka/r5_menu/menu
source /cvmfs/cms.cern.ch/cmsset_default.sh; export SCRAM_ARCH=el9_amd64_gcc13; export TMPDIR=$R/../tmp
if [ $A = stock ]; then cd /cvmfs/cms.cern.ch/el9_amd64_gcc13/cms/cmssw/CMSSW_20_1_0_pre2/src; else cd ${AREA:-/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka/main/CMSSW}/src; fi
eval $(scramv1 runtime -sh) && mkdir -p $O && cd $O || exit 1
echo "start $(date '+%-I:%M:%S %p') load $(cut -d' ' -f1 /proc/loadavg)" > times.txt
$T/patatrack-scripts/benchmark -r ${REP:-1} -j $J -t $TH -s $ST -e $EV --event-skip $SK --event-resolution 10 --no-input-benchmark --debug-logs -k Phase2Timing_resources.json -- $CFG > bench.log 2>&1; echo "benchmark rc=$?"
echo "end $(date '+%-I:%M:%S %p') load $(cut -d' ' -f1 /proc/loadavg)" >> times.txt
cp logs_*/Phase2Timing_resources.json Phase2Timing_resources.json 2>/dev/null
grep -iE "throughput|ev/s" bench.log | tail -3
