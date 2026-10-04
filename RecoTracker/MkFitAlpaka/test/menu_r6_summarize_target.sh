#!/bin/bash
# summarize_target.sh <port run tag> [stock run tag(s) for MTV/trigger/DQM, comma-separated]: target-menu validation summary
# (customizeHLTforMkFitAlpakaTargetValidation): status, device hits vs stock converters, in-job comparators, D-M4 RAW
# against the OLD-reference isolated fit floors (r4_fit; new-reference floors from lane ref when they exist),
# cross-job MTV / TriggerResults / DQM vs the stock arms, module times.
PV=$1; STS=$2; L=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka; R=$L/r6_menu/menu
FL=$L/r4_fit/floor; C=$L/r6_menu/CMSSW/src/RecoTracker/MkFitAlpaka/test/replay_floor_check.py; LOG=$R/runs/$PV/hlt.log; O=$R/results/$PV.txt
SMP=ttbar; [[ $PV == *qcd* ]] && SMP=qcd
source $L/r6_menu/env.sh > /dev/null 2>&1
[ -s $R/harv/$PV.root ] || $R/scripts/harv.sh $PV $R/runs/$PV/DQM_$PV.root > /dev/null
{
echo "TARGET MENU VALIDATION $PV: $(cat $R/runs/$PV/times.txt | tr '\n' ' ')"
echo "=== status product"; grep -A1 "MkFitAlpakaStatusCheck hltInitialStep" $LOG | grep -v "^--" | head -4
echo "=== device hits vs stock converters (per event)"; echo "IDENTICAL lines: $(grep -c 'DEVICE_HITS_EOH.*IDENTICAL' $LOG); other DEVICE_HITS_EOH lines: $(grep 'DEVICE_HITS_EOH' $LOG | grep -vc IDENTICAL)"; grep 'DEVICE_HITS_EOH' $LOG | grep -v IDENTICAL | head -3
echo "=== in-job comparators"; grep -E "^\[compare menuCmp[A-Za-z]*\] (events|tracks|matched pairs|events identical)|^\[fit\] events|^\[paired menu\]" $LOG
for lab in menuCmpFit menuCmpFitSerial menuCmpFitDevSerial; do
  echo "=== D-M4 RAW, $lab vs OLD-reference ISOLATED fit floor (r4_fit floor_${SMP}_100_cpe1_fitiso; pre-#186 stock v2/v3)"
  python3 $C $FL/floor_${SMP}_100_cpe1_fitiso.json $LOG --port-label $lab --no-paired
done
echo "=== D-M4 RAW, menuCmpFitTracks (reco::Track) vs OLD-reference ISOLATED floor (fitisotrk)"; python3 $C $FL/floor_${SMP}_100_cpe1_fitisotrk.json $LOG --port-label menuCmpFitTracks --no-paired
for ST in ${STS//,/ }; do
  [ -s $R/harv/$ST.root ] || $R/scripts/harv.sh $ST $R/runs/$ST/DQM_$ST.root > /dev/null
  echo "=== MTV (harvested): $ST vs $PV"; python3 RecoTracker/MkFitAlpaka/test/replay_mtv_numbers.py $R/harv/$ST.root $R/harv/$PV.root | grep -E "root|hltGeneral|hltInitialStep_|HighPurity"
  echo "=== TriggerResults $ST vs $PV"; python3 $R/scripts/compare_trig.py $R/runs/$ST/hlt.log $LOG
  echo "=== DQM histograms $ST vs $PV"; python3 $R/scripts/compare_dqm.py $R/harv/$ST.root $R/harv/$PV.root
done
echo "=== module times (FastTimerService, ms per job event; 8 threads, shared box: indicative)"; python3 $R/scripts/module_times.py $R/runs/$PV/Phase2Timing_resources.json
} > $O 2>&1; echo "wrote $O"; grep -E "verdict|not clean|IDENTICAL lines" $O
