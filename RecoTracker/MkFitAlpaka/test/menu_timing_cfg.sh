#!/bin/bash
# make_timing_cfg.sh <arm stock|port> <cpu|gpu> <out cfg>: HLT-only timing config (= hltqcd/timing/make_cfg.sh, the
# runHLTTiming-like recipe of lanes/mkfitfit and trkgpu) on the 5 local ttbar files; port = + --customise of our package.
# r5_menu: PROCMOD env adds --procModifiers (trackingMkFitFit menu, D5-c); port area default = lanes/mkfit_alpaka/main (r5/menu = main head)
A=$1; ACC=$2; OUT=$3; R=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka/r5_menu/menu
source /cvmfs/cms.cern.ch/cmsset_default.sh; export SCRAM_ARCH=el9_amd64_gcc13; export TMPDIR=$R/../tmp
if [ $A = stock ]; then cd /cvmfs/cms.cern.ch/el9_amd64_gcc13/cms/cmssw/CMSSW_20_1_0_pre2/src; else cd ${AREA:-/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka/main/CMSSW}/src; fi
eval $(scramv1 runtime -sh) && mkdir -p $(dirname $OUT) && cd $(dirname $OUT) || exit 1
FILES=$(ls /mnt/data1/gsn27/here/ttbar_raw/*.root | sed 's|^|file:|' | paste -sd,)
EXTRA=""; [ "$ACC" = cpu ] && EXTRA="--accelerators cpu"
CUS=(); [ $A != stock ] && CUS=(--customise RecoTracker/MkFitAlpaka/customizeHLTforMkFitAlpaka.${FUNC:-customizeHLTforMkFitAlpaka})
cmsDriver.py Phase2 -s L1P2GT,HLT:75e33_timing --processName=HLTX --conditions auto:phase2_realistic_T35 --geometry ExtendedRun4D121 \
  --era Phase2C22I13M9 ${PROCMOD:+--procModifiers $PROCMOD} --customise SLHCUpgradeSimulations/Configuration/aging.customise_aging_1000 "${CUS[@]}" --eventcontent FEVTDEBUGHLT \
  --filein="$FILES" --mc --nThreads 4 --inputCommands 'keep *, drop *_hlt*_*_HLT, drop triggerTriggerFilterObjectWithRefs_l1t*_*_HLT' \
  -n 500 --no_exec --output {} $EXTRA --python_filename $(basename $OUT) > $(basename $OUT .py).driver.log 2>&1 && echo "made $OUT"
