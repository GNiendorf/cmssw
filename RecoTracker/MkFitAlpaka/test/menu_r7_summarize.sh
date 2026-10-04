#!/bin/bash
# summarize.sh <run tag> [reference run tags, comma-separated, in r7_menu/menu/runs or verify6_int/menu/runs]: round-7 target-menu
# validation summary: status, device hits, in-job comparators + paired truth (vs the IN-JOB stock chain = D7-a reference:
# stock + #186 + #187 + -Ofast MkFitCore + NaN guard), D-M4 vs the floors in $FLOORS (default lane fitgap's D7-a floors;
# replay samples, so not like-for-like: labelled), cross-job MTV / TriggerResults / DQM vs reference runs, module times.
PV=$1; STS=$2; L=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka
R=$L/r7_menu/menu; V6=$L/verify6_int/menu; F=${FLOORS:-$L/r7_fitgap/floors}
C=$L/r7_menu/CMSSW/src/RecoTracker/MkFitAlpaka/test/replay_floor_check.py; LOG=$R/runs/$PV/hlt.log; O=$R/results/$PV.txt
SMP=ttbar; [[ $PV == *qcd* ]] && SMP=qcd
source $L/r7_menu/env.sh > /dev/null 2>&1
[ -s $R/harv/$PV.root ] || $R/scripts/harv.sh $PV $R/runs/$PV/DQM_$PV.root > /dev/null
{
echo "RUN $PV: $(cat $R/runs/$PV/times.txt | tr '\n' ' ')"
echo "events: $(grep -c 'Begin processing' $LOG) processed"
echo "=== status products (requireClean)"; grep -A1 "MkFitAlpakaStatusCheck" $LOG | grep -v "^--" | head -6; echo "status exceptions: $(grep -c 'StatusCheck.*not clean\|requireClean' $LOG)"
echo "=== device hits vs stock converters (per event)"; echo "IDENTICAL lines: $(grep -c 'DEVICE_HITS_EOH.*IDENTICAL' $LOG); other DEVICE_HITS_EOH lines: $(grep 'DEVICE_HITS_EOH' $LOG | grep -vc IDENTICAL)"; grep 'DEVICE_HITS_EOH' $LOG | grep -v IDENTICAL | head -3
echo "=== in-job comparators"; grep -E "^\[compare menuCmp[A-Za-z]*\] (events|tracks|matched pairs|events identical)|^\[paired menu\]" $LOG
for fl in $(ls $F/isolated/fit_${SMP}_*fitiso.json 2>/dev/null); do for lab in menuCmpFit menuCmpFitDevSerial; do
  echo "=== D-M4, $lab vs $(basename $fl) (replay isolated floor, $F)"; python3 $C $fl $LOG --label floorFitIso --port-label $lab --no-paired; done; done
for fl in $(ls $F/isolated/fit_${SMP}_*fitisotrk.json 2>/dev/null); do echo "=== D-M4, menuCmpFitTracks (reco::Track) vs $(basename $fl)"; python3 $C $fl $LOG --label floorFitIsoTrk --port-label menuCmpFitTracks --no-paired; done
for fl in $(ls $F/chained/${SMP}500/floor_${SMP}500_chained.log 2>/dev/null); do echo "=== D-M4, menuCmpMkFit (building) vs $fl"; python3 $C $fl $LOG --label floorCand --port-label menuCmpMkFit --no-paired; done
for ST in ${STS//,/ }; do
  if [ -d $R/runs/$ST ]; then SD=$R; else SD=$V6; fi
  [ -s $SD/harv/$ST.root ] || $R/scripts/harv.sh $ST $SD/runs/$ST/DQM_$ST.root > /dev/null
  H=$SD/harv/$ST.root; [ -s $H ] || H=$R/harv/$ST.root
  echo "=== MTV (harvested): $ST vs $PV"; python3 RecoTracker/MkFitAlpaka/test/replay_mtv_numbers.py $H $R/harv/$PV.root | grep -E "root|hltGeneral|hltInitialStep_|HighPurity|hltSeedsForMkFit"
  echo "=== TriggerResults $ST vs $PV"; python3 $R/scripts/compare_trig.py $SD/runs/$ST/hlt.log $LOG
  echo "=== DQM histograms $ST vs $PV"; python3 $R/scripts/compare_dqm.py $H $R/harv/$PV.root
done
echo "=== module times (FastTimerService, ms per job event; 8 threads, indicative)"; python3 $R/scripts/module_times.py $R/runs/$PV/Phase2Timing_resources.json 2>/dev/null | head -40
} > $O 2>&1; echo "wrote $O"; grep -E "verdict|IDENTICAL lines|status exceptions|paired menu" $O | head
