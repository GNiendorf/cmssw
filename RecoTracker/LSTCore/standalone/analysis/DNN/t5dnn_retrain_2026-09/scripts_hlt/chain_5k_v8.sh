#!/bin/bash
# PR+mkFit+T5DNN (v8) on the full 5k+5k (PR and PR+mkFit 5k exist), harvest, 3-way (+pre-PR) MTV plots and table.
V=/mnt/data1/gsn27/here/hltqcd/val5k; cd $V
python3 scheduler_5k.py > sched_5k_v8.out 2>&1
source /cvmfs/cms.cern.ch/cmsset_default.sh; export SCRAM_ARCH=el9_amd64_gcc13
for s in qcd ttbar; do (
  cd $V/v8_prmkt5/src && eval $(scramv1 runtime -sh); W=$V/harvest_5k/v8_prmkt5/$s; mkdir -p $W && cd $W
  F=$(for t in $(awk '{print $1}' $V/files_$s.txt | xargs -n1 basename | cut -c1-8); do ls $V/runs/v8_prmkt5/$s/$t/DQM_$t.root; done | sed 's|^|file:|' | paste -sd,)
  cmsDriver.py HARVEST -s HARVESTING:@trackingOnlyValidation+@trackingOnlyDQM+postProcessorHLTtrackingSequence \
    --conditions auto:phase2_realistic_T35 --filein $F --scenario pp --filetype DQM --mc -n -1 > harvest.log 2>&1
  mv DQM_V0001_R000000001__Global__CMSSW_X_Y_Z__RECO.root $V/harvested_5k/v8_prmkt5_$s.root && echo "harvested v8 $s ($(echo $F | tr ',' '\n' | wc -l) files)" >> $V/sched_5k.log ) & done; wait
cd $V/v2_pr/src && eval $(scramv1 runtime -sh); cd $V/mtv5k
for s in qcd ttbar; do ln -sf ../harvested_5k/v8_prmkt5_$s.root PR+mkFitFix+T5DNN_$s.root
  python3 $V/cmp_files.py prePR=prePR_$s.root PR=PR_$s.root PR+mkFit=PR+mkFitFix_$s.root PR+mkFit+T5DNN=PR+mkFitFix+T5DNN_$s.root > $V/threeway_5k_$s.txt 2>&1
  makeTrackValidationPlots.py --extended --jobs 8 -o mtv_PR_PR+mkFitFix_PR+mkFitFix+T5DNN_$s PR_$s.root PR+mkFitFix_$s.root PR+mkFitFix+T5DNN_$s.root > mtv3v8_$s.log 2>&1
  makeTrackValidationPlots.py --extended --png --jobs 8 -o png_PR_PR+mkFitFix_PR+mkFitFix+T5DNN_$s PR_$s.root PR+mkFitFix_$s.root PR+mkFitFix+T5DNN_$s.root > mtv3v8png_$s.log 2>&1
  makeTrackValidationPlots.py --extended --png --jobs 8 -o png_prePR_PR_PR+mkFitFix_PR+mkFitFix+T5DNN_$s prePR_$s.root PR_$s.root PR+mkFitFix_$s.root PR+mkFitFix+T5DNN_$s.root > mtv4v8png_$s.log 2>&1
  echo "mtv3 v8 5k $s rc=$?" >> $V/sched_5k.log; done
echo "FINISHED v8 5k" >> $V/sched_5k.log
