#!/bin/bash
# gen.sh <tag> <procmod|none> [customise func]: the EXACT job.sh cmsDriver line (75e33_timing, aging 1000, the hltSeedsForMkFit
# customise_commands), --no_exec, then edmConfigDump -> <tag>_dump.py
L=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka; D=$L/r7_menu/menu/stageD
T=$1; PM=$2; FUNC=$3; mkdir -p $D/$T && cd $D/$T
source /cvmfs/cms.cern.ch/cmsset_default.sh; export SCRAM_ARCH=el9_amd64_gcc13; export TMPDIR=$L/r7_menu/tmp
cd $L/r7_menu/CMSSW/src && eval $(scramv1 runtime -sh) && cd $D/$T
CUS=(); [ "$PM" != none ] && CUS=(--procModifiers $PM); [ -n "$FUNC" ] && CUS+=(--customise RecoTracker/MkFitAlpaka/customizeHLTforMkFitAlpaka.$FUNC)
SETUP="--conditions auto:phase2_realistic_T35 --geometry ExtendedRun4D121 --era Phase2C22I13M9"
IN=file:/mnt/data1/gsn27/here/ttbar_raw/$(ls /mnt/data1/gsn27/here/ttbar_raw | head -1)
cmsDriver.py step2 -s L1P2GT,HLT:75e33_timing,VALIDATION::hltMultiTrackValidation+hltMultiPVValidation --processName=HLTX --hltProcess HLTX $SETUP \
  --customise SLHCUpgradeSimulations/Configuration/aging.customise_aging_1000 "${CUS[@]}" --filein $IN \
  --inputCommands='keep *, drop *_hlt*_*_HLT, drop triggerTriggerFilterObjectWithRefs_l1t*_*_HLT' \
  --eventcontent DQM --datatier DQMIO --fileout file:DQM_$T.root \
  --customise_commands "process.hltTrackValidator.label += [\"hltInitialStepTracks\"]\nprocess.options.wantSummary = True\nprocess.hltSeedsForMkFit = process.hltInitialStepTrajectorySeedsLSTTracks.clone()\nprocess.HLTInitialStepSequence += process.hltSeedsForMkFit\nprocess.hltTrackValidator.label += [\"hltSeedsForMkFit\"]\n" \
  --python_filename hlt.py --mc -n 10 --nThreads 8 --accelerators gpu-nvidia --no_exec > cfg.log 2>&1 || { echo FAIL-CFG; exit 1; }
edmConfigDump hlt.py > $D/${T}_dump.py 2> dump.err; echo "dump rc=$? $(wc -l < $D/${T}_dump.py) lines"
