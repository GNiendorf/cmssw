#!/bin/bash
# run_def.sh <bin> <tag> [overrides...]  -- ANCHOR+CTL only (defaults gate).
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
D=$S/fanout5/ex_extend
ANCHOR="-e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9 -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2 -C25 2.0 -C25D -2.0"
CTL="-A 4 -a 999 -D 5 -RT5 1"
BIN=$1; shift
TAG=$1; shift
echo "BIN: $BIN OVERRIDES: $*" > $D/$TAG.cmd
$BIN -m hybrid \
  -i $S/LSTNtuple_PU200RelVal_300evt.root \
  -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
  -o $D/$TAG.root $ANCHOR $CTL "$@" > $D/$TAG.log 2>&1 || { echo "FAIL $TAG"; exit 1; }
createPerfNumDenHists -i $D/$TAG.root -o $D/${TAG}_hists.root >> $D/$TAG.log 2>&1
python3 $S/fanout4/compose_attach/compare_ab.py --proto $D/${TAG}_hists.root \
  --base $S/prototype/base300_hists.root --json $D/$TAG.json > $D/agg_$TAG.txt 2>/dev/null
echo "DONE $TAG"
