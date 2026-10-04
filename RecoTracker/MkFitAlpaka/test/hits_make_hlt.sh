#!/bin/bash
# Generates hlt.py (stock Phase-2 HLT 75e33_timing, CPU, as lanes/pre2_4way/scripts/job.sh) in the current directory;
# test/hitsDump_cfg.py and test/hitsEventOfHits_cfg.py load it. Usage: hits_make_hlt.sh <input RAW file>
IN=$1
SETUP="--conditions auto:phase2_realistic_T35 --geometry ExtendedRun4D121 --era Phase2C22I13M9"
cmsDriver.py step2 -s L1P2GT,HLT:75e33_timing --processName=HLTX --hltProcess HLTX $SETUP \
  --customise SLHCUpgradeSimulations/Configuration/aging.customise_aging_1000 --filein file:$IN \
  --inputCommands='keep *, drop *_hlt*_*_HLT, drop triggerTriggerFilterObjectWithRefs_l1t*_*_HLT' \
  --eventcontent DQM --datatier DQMIO --fileout file:DQM_unused.root \
  --python_filename hlt.py --mc -n 10 --nThreads 8 --accelerators cpu --no_exec
