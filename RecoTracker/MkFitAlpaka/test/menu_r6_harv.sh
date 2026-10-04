#!/bin/bash
# harv.sh <outname> <DQM files...>: = lanes/pre2_4way/scripts/harv.sh (val5k harvest + postProcessorHLTvertexing), run in
# the release env, output r6_menu/menu/harv/<outname>.root
R=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka/r6_menu/menu
O=$1; shift; H=$R/harv/$O; rm -rf $H; mkdir -p $H && cd $H || exit 1
source /cvmfs/cms.cern.ch/cmsset_default.sh; export SCRAM_ARCH=el9_amd64_gcc13; export TMPDIR=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka/r6_menu/tmp
cd /cvmfs/cms.cern.ch/el9_amd64_gcc13/cms/cmssw/CMSSW_20_1_0_pre2/src && eval $(scramv1 runtime -sh) && cd $H || exit 1
F=$(for f in "$@"; do echo -n "file:$f,"; done | sed 's/,$//')
cmsDriver.py HARVEST -s HARVESTING:@trackingOnlyValidation+@trackingOnlyDQM+postProcessorHLTtrackingSequence+postProcessorHLTvertexing --conditions auto:phase2_realistic_T35 --filein $F --scenario pp --filetype DQM --mc -n -1 > harvest.log 2>&1
mv DQM_V0001_R000000001__Global__CMSSW_X_Y_Z__RECO.root ../$O.root && { cd ..; rm -rf $H; echo "OK $O ($# files)"; }
