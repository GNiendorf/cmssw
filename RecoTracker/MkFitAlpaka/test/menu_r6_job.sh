#!/bin/bash
# menu_job.sh <tag> <area CMSSW dir> <acc cpu|gpu-nvidia> <sample ttbar|qcd> <nev> [customise func]
# ONE-STEP HLT+DQM with the EXACT command line of lanes/pre2_4way/scripts/job.sh (75e33_timing) as in
# verify5_int/menu/scripts/menu_job_fit.sh: --procModifiers trackingMkFitFit on every arm (the target menu family);
# PROCMOD=none: no procModifier (the default KF menu). --nThreads 8 (box rule); local RAW (ttbar 5 x 100 ev, qcd 10 x 50 ev); func empty = stock.
TAG=$1; AREA=$2; ACC=$3; SMP=$4; N=${5:--1}; FUNC=$6
L=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka; R=$L/r6_menu/menu
D=$R/runs/$TAG; mkdir -p $D && cd $D || exit 1
case $SMP in
  ttbar) IN=$(ls /mnt/data1/gsn27/here/ttbar_raw/*.root | sed 's|^|file:|' | paste -sd,);;
  qcd)   IN=$(ls /mnt/data1/gsn27/here/qcd_raw/*.root | sed 's|^|file:|' | paste -sd,);;
esac
source /cvmfs/cms.cern.ch/cmsset_default.sh; export SCRAM_ARCH=el9_amd64_gcc13; export TMPDIR=$L/r6_menu/tmp
cd $AREA/src && eval $(scramv1 runtime -sh) && cd $D || exit 1
CUS=(); [ "${PROCMOD:-trackingMkFitFit}" != none ] && CUS=(--procModifiers ${PROCMOD:-trackingMkFitFit}); [ -n "$FUNC" ] && CUS+=(--customise RecoTracker/MkFitAlpaka/customizeHLTforMkFitAlpaka.$FUNC)
SETUP="--conditions auto:phase2_realistic_T35 --geometry ExtendedRun4D121 --era Phase2C22I13M9"
XC="${XC:-}"
cmsDriver.py step2 -s L1P2GT,HLT:75e33_timing,VALIDATION::hltMultiTrackValidation+hltMultiPVValidation --processName=HLTX --hltProcess HLTX $SETUP \
  --customise SLHCUpgradeSimulations/Configuration/aging.customise_aging_1000 "${CUS[@]}" --filein $IN \
  --inputCommands='keep *, drop *_hlt*_*_HLT, drop triggerTriggerFilterObjectWithRefs_l1t*_*_HLT' \
  --eventcontent DQM --datatier DQMIO --fileout file:DQM_$TAG.root \
  --customise_commands "process.hltTrackValidator.label += [\"hltInitialStepTracks\"]\nprocess.options.wantSummary = True\nprocess.hltSeedsForMkFit = process.hltInitialStepTrajectorySeedsLSTTracks.clone()\nprocess.HLTInitialStepSequence += process.hltSeedsForMkFit\nprocess.hltTrackValidator.label += [\"hltSeedsForMkFit\"]\n$XC\n" \
  --python_filename hlt.py --mc -n $N --nThreads 8 --accelerators $ACC --no_exec > hlt_cfg.log 2>&1 || { echo "FAIL-CFG $TAG"; exit 1; }
echo "start $(date '+%-I:%M:%S %p') load $(cut -d' ' -f1 /proc/loadavg)" > times.txt
/usr/bin/time -v cmsRun hlt.py > hlt.log 2>&1; rc=$?
echo "end $(date '+%-I:%M:%S %p') rc=$rc load $(cut -d' ' -f1 /proc/loadavg)" >> times.txt
[ $rc -eq 0 ] && [ -s DQM_$TAG.root ] || { echo "FAIL-HLT $TAG rc=$rc"; exit 1; }
echo "OK $TAG"
