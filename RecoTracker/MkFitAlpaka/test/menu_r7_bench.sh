#!/bin/bash
# bench.sh <area CMSSW dir> <cfg> <outdir> [jobs threads streams events skip]: patatrack benchmark (= hltqcd/timing/bench.sh)
AREA=$1; CFG=$2; O=$3; J=${4:-4}; TH=${5:-16}; ST=${6:-16}; EV=${7:-300}; SK=${8:-50}; T=/mnt/data1/gsn27/here/hltqcd/timing
L=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka
source /cvmfs/cms.cern.ch/cmsset_default.sh; export SCRAM_ARCH=el9_amd64_gcc13; export TMPDIR=$L/r7_menu/tmp
cd $AREA/src && eval $(scramv1 runtime -sh) && mkdir -p $O && cd $O || exit 1
echo "start $(date '+%-I:%M:%S %p') load $(cut -d' ' -f1 /proc/loadavg) area $AREA" > times.txt
[ -n "$GPUMEM" ] && { /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka/r7_menu/menu/scripts/gpumem2.sh $O $O/gpumem.txt & gm=$!; }
$T/patatrack-scripts/benchmark -r ${REP:-1} -j $J -t $TH -s $ST -e $EV --event-skip $SK --event-resolution 10 --no-input-benchmark --debug-logs -k Phase2Timing_resources.json -- $CFG > bench.log 2>&1; echo "benchmark rc=$?" >> times.txt
[ -n "$gm" ] && kill $gm
echo "end $(date '+%-I:%M:%S %p') load $(cut -d' ' -f1 /proc/loadavg)" >> times.txt
cp logs_*/Phase2Timing_resources.json Phase2Timing_resources.json 2>/dev/null
grep -E "ev/s" bench.log | tail -1
