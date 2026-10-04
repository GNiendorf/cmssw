#!/bin/bash
# job.sh <arm> <smp> <input RAW> <tag> [nevents]: lanes/pre2_4way/scripts/job.sh (the val5k command line) EXACTLY, with lane dirs, and the
# arm's extra cmsDriver arguments from cfg/<arm>.cust inserted after the aging customise (r7: --procModifiers trackingMkFitFit [+ --customise
# ...customizeHLTforMkFitAlpakaTarget]) and the accelerator from cfg/<arm>.acc (default cpu; tgpu: gpu-nvidia, CUDA_VISIBLE_DEVICES from cfg/<arm>.gpu).
# areas/<arm> -> areas/int (setup_area.sh). After the job: MkFitAlpaka status warnings -> status.txt.
A=$1; SMP=$2; IN=$3; T=$4; N=${5:--1}
L=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka/r7_menu/val5k
D=$L/runs/$A/$SMP/$T; XC=""; [ -s $L/cfg/$A.xc ] && XC="$(cat $L/cfg/$A.xc)"; CUS=""; [ -s $L/cfg/$A.cust ] && CUS="$(cat $L/cfg/$A.cust)"; ACC=cpu; [ -s $L/cfg/$A.acc ] && ACC="$(cat $L/cfg/$A.acc)"; [ -s $L/cfg/$A.gpu ] && export CUDA_VISIBLE_DEVICES="$(cat $L/cfg/$A.gpu)"; mkdir -p $D && cd $D || exit 1
[ "$(stat -c %s DQM_$T.root 2>/dev/null || echo 0)" -gt 1000000 ] && { echo "SKIP $A $SMP $T"; exit 0; }
source /cvmfs/cms.cern.ch/cmsset_default.sh; export SCRAM_ARCH=el9_amd64_gcc13; export TMPDIR=$L/tmp
cd $L/areas/$A/src && eval $(scramv1 runtime -sh) && cd $D || exit 1
FIN=file:$IN; case $IN in root://*) FIN=$IN;; esac   # lane: xrootd streaming input (scheduler STREAM mode)
SETUP="--conditions auto:phase2_realistic_T35 --geometry ExtendedRun4D121 --era Phase2C22I13M9"
cmsDriver.py step2 -s L1P2GT,HLT:75e33_timing,VALIDATION::hltMultiTrackValidation+hltMultiPVValidation --processName=HLTX --hltProcess HLTX $SETUP \
  --customise SLHCUpgradeSimulations/Configuration/aging.customise_aging_1000 $CUS --filein $FIN \
  --inputCommands='keep *, drop *_hlt*_*_HLT, drop triggerTriggerFilterObjectWithRefs_l1t*_*_HLT' \
  --eventcontent DQM --datatier DQMIO --fileout file:DQM_$T.root \
  --customise_commands "process.hltTrackValidator.label += [\"hltInitialStepTracks\"]\nprocess.options.wantSummary = True\nprocess.hltSeedsForMkFit = process.hltInitialStepTrajectorySeedsLSTTracks.clone()\nprocess.HLTInitialStepSequence += process.hltSeedsForMkFit\nprocess.hltTrackValidator.label += [\"hltSeedsForMkFit\"]\n$XC\n" \
  --python_filename hlt.py --mc -n $N --nThreads 16 --accelerators $ACC --no_exec > hlt_cfg.log 2>&1 || { echo "FAIL-CFG $A $SMP $T"; exit 1; }
/usr/bin/time -v cmsRun hlt.py > hlt.log 2>&1; rc=$?
echo "hlt $(grep 'Maximum resident' hlt.log | awk '{print $NF}') $(grep 'Elapsed (wall' hlt.log | awk '{print $NF}')" > peak.txt
echo "status_warnings $(grep -c 'MkFitAlpakaStatus\|MkFitAlpakaBuild\|MkFitAlpakaOutputWrapper' hlt.log) device_modules $(grep -c 'MkFitAlpakaBuildProducer\|MkFitAlpakaEventOfHitsProducer' hlt.py)" > status.txt
[ $rc -eq 0 ] && [ -s DQM_$T.root ] || { rm -f DQM_$T.root; echo "FAIL-HLT $A $SMP $T rc=$rc"; exit 1; }
gzip -f hlt.log
echo "OK $A $SMP $T"
