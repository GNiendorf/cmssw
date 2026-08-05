#!/bin/bash
# ex_run.sh <tag> [overrides...]
# Runs FLAGSHIP base with overrides appended last (later flags win).
# BIN env var selects the binary: default = GOLDEN (read-only), BIN=local = ex_dupwin build.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
D=$S/fanout5/ex_dupwin
ANCHOR="-e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9 -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2 -C25 2.0 -C25D -2.0"
CTL="-A 4 -a 999 -D 5 -RT5 1"
STACK="-BK 1 -BT 5 -TR 1 -TT 0.8 -TA 1.0 -F 0.20 -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1 -C25 0.0 -ZM4D 1.2 -ZM4 -0.5"
FLAG="-TT 1.2 -a 8 -RPS 1 -RD 1"
BASE="${BASE:-flagship}"
if [ "$BASE" = "defaults" ]; then STACK=""; FLAG=""; fi
EXE="$S/fanout4/compose_attach/bin/chainproto"
if [ "$BIN" = "local" ]; then EXE="$D/bin/chainproto"; fi
TAG=$1; shift
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
echo "EXE: $EXE" > $D/x_$TAG.cmd
echo "BASE: $BASE" >> $D/x_$TAG.cmd
echo "OVERRIDES: $*" >> $D/x_$TAG.cmd
rm -f $D/x_$TAG.root $D/x_${TAG}_hists.root
$EXE -m hybrid \
  -i $S/LSTNtuple_PU200RelVal_300evt.root \
  -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
  -o $D/x_$TAG.root $ANCHOR $CTL $STACK $FLAG "$@" > $D/x_$TAG.log 2>&1 || { echo "FAIL $TAG"; tail -20 $D/x_$TAG.log; exit 1; }
createPerfNumDenHists -i $D/x_$TAG.root -o $D/x_${TAG}_hists.root >> $D/x_$TAG.log 2>&1
python3 $S/fanout4/compose_attach/compare_ab.py --proto $D/x_${TAG}_hists.root \
  --base $S/prototype/base300_hists.root --json $D/x_$TAG.json > $D/x_agg_$TAG.txt 2>/dev/null
echo "DONE $TAG"
