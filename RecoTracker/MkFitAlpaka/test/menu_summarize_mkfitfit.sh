#!/bin/bash
# summarize_mkfitfit.sh <run> <smp>: trackingMkFitFit menu run (queue2.sh): status, in-job comparisons, D-M4 vs the same-sample floors:
# mkFit level (menuCmpMkFit) vs stagefloor 'final'; mkFit-fit reco::Tracks (menuCmpTracks = MkFitOutputTrackConverter output) vs floor500
# floorFit (the floor's hltInitialStepTracksMkFitFit: the SAME module chain, so here it is a direct floor, not a proxy).
PV=$1; SMP=$2
L=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka; R=$L/r5_menu/menu
F=$L/r3_harness/floor500; S=$L/r3_harness/stagefloor; C=$L/main/CMSSW/src/RecoTracker/MkFitAlpaka/test/replay_floor_check.py
LOG=$R/runs/$PV/hlt.log; O=$R/results/$PV.txt
source /cvmfs/cms.cern.ch/cmsset_default.sh; export SCRAM_ARCH=el9_amd64_gcc13; cd $L/main/CMSSW/src && eval $(scramv1 runtime -sh)
if [ $SMP = ttbar ]; then FC=$F/tgt_mkfit_ttbar.log; SF=$S/floor_ttbar500_final.json; else FC=$F/tgt_mkfit_qcd.log; SF=$S/floor_qcd500_final.json; fi
{ echo "MENU trackingMkFitFit $PV ($(cat $R/runs/$PV/info.txt)) $(cat $R/runs/$PV/times.txt | tr '\n' ' ')"
  echo "=== status"; cat $R/runs/$PV/status.json; echo
  echo "=== in-job per-seed comparisons"; grep -E "^\[compare menuCmp[A-Za-z]+\] (events|tracks|matched pairs|events identical)" $LOG
  echo "=== D-M4: mkFit level vs stage 'final' (100 ev)"; python3 $C $SF $LOG --port-label menuCmpMkFit --no-paired
  echo "=== D-M4: mkFit-fit tracks vs floor500 floorFit (direct floor)"; python3 $C $FC $LOG --label floorFit --port-label menuCmpTracks --no-paired
  echo "=== module times"; python3 $R/scripts/module_times.py $R/runs/$PV/Phase2Timing_resources.json; } > $O 2>&1
grep -E "verdict|WORSE" $O
