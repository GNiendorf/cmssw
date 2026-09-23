#!/bin/bash
# After the v8 1k run: harvest v8 on the same 1k events per sample, then 3-way MTV (PR, PR+mkFit, PR+mkFit+T5DNN).
V=/mnt/data1/gsn27/here/hltqcd/val5k; cd $V
START=$(grep -n "=== v8 run" sched_1k.log | tail -1 | cut -d: -f1)
until tail -n +$START sched_1k.log | grep -q "ALL DONE"; do sleep 30; done
source /cvmfs/cms.cern.ch/cmsset_default.sh; export SCRAM_ARCH=el9_amd64_gcc13
for s in qcd ttbar; do (
  cd $V/v8_prmkt5/src && eval $(scramv1 runtime -sh); W=$V/harvest_1k/v8_prmkt5/$s; mkdir -p $W && cd $W
  F=$(for t in $(awk '{print $1}' $V/files_1k_$s.txt | xargs -n1 basename | cut -c1-8); do ls $V/runs/v8_prmkt5/$s/$t/DQM_$t.root; done | sed 's|^|file:|' | paste -sd,)
  cmsDriver.py HARVEST -s HARVESTING:@trackingOnlyValidation+@trackingOnlyDQM+postProcessorHLTtrackingSequence \
    --conditions auto:phase2_realistic_T35 --filein $F --scenario pp --filetype DQM --mc -n -1 > harvest.log 2>&1
  mv DQM_V0001_R000000001__Global__CMSSW_X_Y_Z__RECO.root $V/harvested_1k/v8_prmkt5_${s}.root && echo "harvested v8_prmkt5 $s ($(echo $F | tr ',' '\n' | wc -l) files)" >> $V/sched_1k.log ) & done; wait
cd $V/v2_pr/src && eval $(scramv1 runtime -sh); cd $V/mtv1k
for s in qcd ttbar; do ln -sf ../harvested_1k/v8_prmkt5_$s.root PR+mkFitFix+T5DNN_$s.root
  makeTrackValidationPlots.py --extended --jobs 8 -o mtv_PR_PR+mkFitFix_PR+mkFitFix+T5DNN_$s PR_$s.root PR+mkFitFix_$s.root PR+mkFitFix+T5DNN_$s.root > mtv3v8_$s.log 2>&1
  makeTrackValidationPlots.py --extended --png --jobs 8 -o png_PR_PR+mkFitFix_PR+mkFitFix+T5DNN_$s PR_$s.root PR+mkFitFix_$s.root PR+mkFitFix+T5DNN_$s.root > mtv3v8png_$s.log 2>&1
  echo "mtv3 v8 $s rc=$?" >> $V/sched_1k.log; done
echo "FINISHED v8 1k" >> $V/sched_1k.log
