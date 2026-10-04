#!/bin/bash
# memattr.sh <arm> <area> <cfg>: ONE job, 16 threads x 16 streams, 250 events, GPU 0 (L40): device memory of this cmsRun process
# sampled every 1 s (nvidia-smi by pid) -> peak / last; for the D7-f attribution (stockfit vs build-only vs target).
A=$1; AREA=$2; CFG=$3; L=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka
O=$L/r7_menu/menu/memattr/$A; mkdir -p $O && cd $O || exit 1
source /cvmfs/cms.cern.ch/cmsset_default.sh; export SCRAM_ARCH=el9_amd64_gcc13; export TMPDIR=$L/r7_menu/tmp
cd $AREA/src && eval $(scramv1 runtime -sh) && cd $O || exit 1
cat > run_cfg.py <<PY
exec(open("$CFG").read())
process.options.numberOfThreads = 16
process.options.numberOfStreams = 16
process.maxEvents.input = 250
process.MessageLogger.cerr.FwkReport.reportEvery = 50
PY
CUDA_VISIBLE_DEVICES=0 cmsRun run_cfg.py > run.log 2>&1 & p=$!
while kill -0 $p 2>/dev/null; do m=$(nvidia-smi --query-compute-apps=pid,used_memory --format=csv,noheader,nounits | awk -F', ' -v p=$p '$1==p{print $2}'); echo "$(date +%s) ${m:-0}" >> mem.txt; sleep 1; done
wait $p; rc=$?
echo "$A rc=$rc peak_MiB $(awk '{if($2>m)m=$2} END{print m+0}' mem.txt) samples $(wc -l < mem.txt) events $(grep -c 'Begin processing' run.log)"
