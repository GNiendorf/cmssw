#!/bin/bash
# replay_make.sh <smp> <input RAW> [nevents] [outdir]: (round-1 lane_harness/scripts/make_replay.sh, output dir as argument) run the HLT exactly as lanes/pre2_4way/scripts/job.sh (vA area, stock mkFit,
# CPU accelerators) + one extra PoolOutputModule that keeps the LST-step mkFit inputs + fidelity references (+ small gen info).
# Legacy rechits are NOT kept: BaseTrackerRecHit pos_/err_ are transient (read back as 0). The replay re-runs the menu's
# hltSiPixelRecHits (from the kept SoA hltPhase2SiPixelRecHitsSoA) and hltSiPhase2RecHits (from the clusters).
# replayForceTracking: an extra path that runs the HLT tracking on every event (the menu runs it only on L1-seeded
# paths: 70/100 ttbar events); deterministic modules, so events where the menu ran tracking are unchanged.
# The big sim truth (TrackingParticles, simlinks: ~55 MB/event) stays in the RAW parent: read it with secondaryFileNames=<RAW>.
# <input RAW> may be a comma-separated list of file: URLs.
SMP=$1; IN=$2; N=${3:-100}; ODIR=${4:-$L/data}
L=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka
H=$(readlink -f $ODIR)/..; VA=$L/../pre2_4way/areas/vA
D=$(readlink -f $ODIR)/run_$SMP; mkdir -p $D && cd $D || exit 1
source /cvmfs/cms.cern.ch/cmsset_default.sh; export SCRAM_ARCH=el9_amd64_gcc13; export TMPDIR=${TMPDIR:-$D}
cd $VA/src && eval $(scramv1 runtime -sh) && cd $D || exit 1
SETUP="--conditions auto:phase2_realistic_T35 --geometry ExtendedRun4D121 --era Phase2C22I13M9"
OUTF=$(readlink -f $ODIR)/replay_$SMP.root
XC="process.replayOut = cms.OutputModule('PoolOutputModule', fileName = cms.untracked.string('file:$OUTF'), outputCommands = cms.untracked.vstring('drop *', 'keep *_hltInitialStepTrajectorySeedsLST_*_HLTX', 'keep *_hltSiPixelClusters_*_HLTX', 'keep *_hltPhase2SiPixelRecHitsSoA_*_HLTX', 'keep *_hltSiPhase2Clusters_*_HLTX', 'keep *_hltOnlineBeamSpot_*_HLTX', 'keep *_hltInitialStepTrackCandidates_*_HLTX', 'keep *_hltInitialStepTracks_*_HLTX', 'keep *_hltInitialStepTrackSelectionHighPurity_*_HLTX', 'keep *_addPileupInfo_*_*', 'keep *_genParticles_*_*', 'keep *_generator_*_*'))\nprocess.replayOutPath = cms.EndPath(process.replayOut)\nprocess.schedule.append(process.replayOutPath)\nprocess.replayForceTracking = cms.Path(process.HLTBeginSequence + process.HLTRawToDigiSequence + process.HLTLocalrecoSequence + process.HLTTrackingSequence)\nprocess.schedule.insert(0, process.replayForceTracking)"
cmsDriver.py step2 -s L1P2GT,HLT:75e33_timing,VALIDATION::hltMultiTrackValidation+hltMultiPVValidation --processName=HLTX --hltProcess HLTX $SETUP \
  --customise SLHCUpgradeSimulations/Configuration/aging.customise_aging_1000 --filein $IN \
  --inputCommands='keep *, drop *_hlt*_*_HLT, drop triggerTriggerFilterObjectWithRefs_l1t*_*_HLT' \
  --eventcontent DQM --datatier DQMIO --fileout file:DQM_$SMP.root \
  --customise_commands "process.hltTrackValidator.label += [\"hltInitialStepTracks\"]\nprocess.options.wantSummary = True\nprocess.hltSeedsForMkFit = process.hltInitialStepTrajectorySeedsLSTTracks.clone()\nprocess.HLTInitialStepSequence += process.hltSeedsForMkFit\nprocess.hltTrackValidator.label += [\"hltSeedsForMkFit\"]\n$XC\n" \
  --python_filename hlt.py --mc -n $N --nThreads 8 --accelerators cpu --no_exec > hlt_cfg.log 2>&1 || { echo "FAIL-CFG $SMP"; exit 1; }
/usr/bin/time -v cmsRun hlt.py > hlt.log 2>&1; rc=$?
echo "hlt $(grep 'Maximum resident' hlt.log | awk '{print $NF}') $(grep 'Elapsed (wall' hlt.log | awk '{print $NF}')" > peak.txt
[ $rc -eq 0 ] && [ -s $OUTF ] || { echo "FAIL-HLT $SMP rc=$rc"; exit 1; }
echo "OK $SMP $(stat -c %s $OUTF)"
