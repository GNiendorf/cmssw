#!/bin/bash
# crossjob.sh <runA> <runB> <out name>: cross-job comparison of two menu runs (harvested MTV numbers, TriggerResults, DQM histograms incl.
# vertex validation) -> results/<out>.txt. Used for the GPU run-to-run floor (stock GPU menu twice) and port-vs-stock cross-job checks.
A=$1; B=$2; O=$3
L=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka; R=$L/r5_menu/menu
for x in $A $B; do [ -s $R/harv/$x.root ] || $R/scripts/harv.sh $x $R/runs/$x/DQM_$x.root; done
source /cvmfs/cms.cern.ch/cmsset_default.sh; export SCRAM_ARCH=el9_amd64_gcc13; cd $L/main/CMSSW/src && eval $(scramv1 runtime -sh)
{ echo "CROSS-JOB $A vs $B"; cat $R/runs/$A/info.txt $R/runs/$B/info.txt
  echo "=== MTV"; python3 $L/main/CMSSW/src/RecoTracker/MkFitAlpaka/test/replay_mtv_numbers.py $R/harv/$A.root $R/harv/$B.root | grep -E "root|hltGeneral|hltInitialStep_|HighPurity|hltSeedsForMkFit"
  echo "=== TriggerResults"; python3 $R/scripts/compare_trig.py $R/runs/$A/hlt.log $R/runs/$B/hlt.log
  echo "=== DQM histograms"; python3 $R/scripts/compare_dqm.py $R/harv/$A.root $R/harv/$B.root; } > $R/results/$O.txt 2>&1
grep -E "paths with|Vertexing|hltGeneral" $R/results/$O.txt | head
