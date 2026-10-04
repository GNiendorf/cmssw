#!/bin/bash
# replay_mtv_harvest.sh <out.root> <DQM files...>: harvest MTV DQM files from replay_mtv_cfg.py (same recipe as the
# val5k/pre2_4way harvest), run inside a CMSSW environment. Writes <out.root>.
O=$(readlink -f $1); shift
F=$(for f in "$@"; do echo -n "file:$(readlink -f $f),"; done | sed 's/,$//')
W=$(mktemp -d ${TMPDIR:-.}/harv.XXXXXX) && cd $W || exit 1
cmsDriver.py HARVEST -s HARVESTING:@trackingOnlyValidation+@trackingOnlyDQM+postProcessorHLTtrackingSequence \
  --conditions auto:phase2_realistic_T35 --filein $F --scenario pp --filetype DQM --mc -n -1 > harvest.log 2>&1
if [ -s DQM_V0001_R000000001__Global__CMSSW_X_Y_Z__RECO.root ]; then
  mv DQM_V0001_R000000001__Global__CMSSW_X_Y_Z__RECO.root $O && cd / && rm -rf $W && echo "OK $O"
else
  echo "FAIL see $W/harvest.log"; exit 1
fi
