#!/bin/bash
# usage: run_val.sh <arm> <qcd|ttbar> <raw.root> <tag>  -- HLT (75e33_timing, cpu) then DQM MTV for one file in area val5k/<arm>.
# Peak RSS of each cmsRun goes to peak.txt; the HLT output is deleted after the DQM step.
A=$1; SMP=$2; IN=$3; T=$4; V=/mnt/data1/gsn27/here/hltqcd/val5k
D=$V/runs/$A/$SMP/$T; mkdir -p $D && cd $D || exit 1
[ "$(stat -c %s DQM_$T.root 2>/dev/null || echo 0)" -gt 1000000 ] && { echo "SKIP $A $SMP $T"; exit 0; }
source /cvmfs/cms.cern.ch/cmsset_default.sh; export SCRAM_ARCH=el9_amd64_gcc13
cd $V/$A/src && eval $(scramv1 runtime -sh) && cd $D || exit 1
SETUP="--conditions auto:phase2_realistic_T35 --geometry ExtendedRun4D121 --era Phase2C22I13M9"
if [ ! -f HLT_DONE ]; then
  cmsDriver.py Phase2 -s L1P2GT,HLT:75e33_timing --processName=HLTX $SETUP --eventcontent FEVTDEBUGHLT \
    --customise SLHCUpgradeSimulations/Configuration/aging.customise_aging_1000 --filein file:$IN \
    --inputCommands='keep *, drop *_hlt*_*_HLT, drop triggerTriggerFilterObjectWithRefs_l1t*_*_HLT' \
    --python_filename hlt.py --fileout file:hlt.root --mc -n -1 --nThreads 16 --accelerators cpu --no_exec > hlt_cfg.log 2>&1 || { echo "FAIL-CFG $A $SMP $T"; exit 1; }
  /usr/bin/time -v cmsRun hlt.py > hlt.log 2>&1; rc=$?
  echo "hlt $(grep 'Maximum resident' hlt.log | awk '{print $NF}')" > peak.txt
  [ $rc -eq 0 ] || { rm -f hlt.root; echo "FAIL-HLT $A $SMP $T rc=$rc"; exit 1; }
  touch HLT_DONE
fi
cmsDriver.py DQM -s VALIDATION::hltMultiTrackValidation+hltMultiPVValidation --hltProcess HLTX $SETUP \
  --eventcontent DQM --datatier DQMIO --filein file:hlt.root --fileout file:DQM_$T.root \
  --python_filename dqm.py -n -1 --nThreads 16 --no_exec > dqm_cfg.log 2>&1 || { echo "FAIL-DQMCFG $A $SMP $T"; exit 1; }
/usr/bin/time -v cmsRun dqm.py > dqm.log 2>&1; rc=$?
echo "dqm $(grep 'Maximum resident' dqm.log | awk '{print $NF}')" >> peak.txt
[ $rc -eq 0 ] && [ -s DQM_$T.root ] || { echo "FAIL-DQM $A $SMP $T rc=$rc"; exit 1; }
rm -f hlt.root
echo "OK $A $SMP $T"
