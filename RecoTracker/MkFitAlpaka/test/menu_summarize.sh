#!/bin/bash
# summarize.sh <port run tag> <smp> <stock harvest name> [floor kind]: results/<tag>.txt = in-job per-seed comparisons (port vs the stock chain
# in the SAME job), D-M4 against the CHAINED floor500 of the SAME sample (r3_harness: stock v3 vs v2 mkFit libraries, replay of exactly
# these RAW events with the vA LST seeds, replay == HLT bit for bit), status product, MTV / TriggerResults / DQM vs a separate stock job.
PV=$1; SMP=$2; STH=$3
L=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka; R=$L/r5_menu/menu
F=$L/r3_harness/floor500; S=$L/r3_harness/stagefloor; C=$L/main/CMSSW/src/RecoTracker/MkFitAlpaka/test/replay_floor_check.py
LOG=$R/runs/$PV/hlt.log; O=$R/results/$PV.txt
[ -s $R/harv/$PV.root ] || $R/scripts/harv.sh $PV $R/runs/$PV/DQM_$PV.root
source /cvmfs/cms.cern.ch/cmsset_default.sh; export SCRAM_ARCH=el9_amd64_gcc13; cd $L/main/CMSSW/src && eval $(scramv1 runtime -sh)
if [ $SMP = ttbar ]; then FC=$F/tgt_mkfit_ttbar.log; FP=$F/tgt_mkfit_paired_ttbar.log; SF=$S/floor_ttbar500_final.json
else FC=$F/tgt_mkfit_qcd.log; FP=$L/r3_harness/floor500q/tgt_mkfit_qcd.log; SF=$S/floor_qcd500_final.json; fi
{
echo "MENU $PV ($(cat $R/runs/$PV/info.txt)) $(cat $R/runs/$PV/times.txt | tr '\n' ' ')"
echo "=== status product (MkFitAlpakaStatusCheck requireClean; status.json)"; cat $R/runs/$PV/status.json 2>/dev/null; echo; grep -c "MkFitAlpakaStatus" $LOG | sed 's/^/MkFitAlpakaStatus warnings: /'
echo "=== in-job per-seed comparisons (stock reference in the same job)"; grep -E "^\[compare menuCmp[A-Za-z]+\] (events|tracks|matched pairs|events identical)|^\[paired" $LOG
echo "=== D-M4 (CHAINED floor, same sample): TrackCandidates vs floor500 floorCand + paired truth vs the floor's paired truth"
python3 $C $FC $LOG --label floorCand --port-label menuCmpCands --floor-paired $FP --port-paired $LOG
echo "=== D-M4: mkFit-level output vs the per-stage CHAINED floor 'final' (100 ev of the same sample)"; python3 $C $SF $LOG --port-label menuCmpMkFit --no-paired
echo "=== KF hltInitialStepTracks vs floor500 floorFit (PROXY: the mkFit-fit floor; no KF-fit floor exists)"; python3 $C $FC $LOG --label floorFit --port-label menuCmpTracks --no-paired
if [ -n "$STH" ]; then
echo "=== MTV (harvested; stock = $STH, a separate job)"; python3 $L/main/CMSSW/src/RecoTracker/MkFitAlpaka/test/replay_mtv_numbers.py $R/harv/$STH.root $R/harv/$PV.root | grep -E "root|hltGeneral|hltInitialStep_|HighPurity|hltSeedsForMkFit"
echo "=== DQM histograms (vertex validation hltMultiPVValidation, MTV) vs $STH"; python3 $R/scripts/compare_dqm.py $R/harv/$STH.root $R/harv/$PV.root
fi
echo "=== module times (FastTimerService, this job; indicative, shared box)"; python3 $R/scripts/module_times.py $R/runs/$PV/Phase2Timing_resources.json 2>&1 | head -30
} > $O 2>&1; grep -E "verdict|VERDICT|PASS|FAIL" $O | head -20
