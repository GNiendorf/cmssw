#!/bin/bash
# sk_run.sh <tag> <binary-path> <base:flagship|defaults> [overrides...]
# Independent skeptic re-derivation. Writes ONLY into fanout5/skeptic1/.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
D=$S/fanout5/skeptic1
ANCHOR="-e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9 -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2 -C25 2.0 -C25D -2.0"
CTL="-A 4 -a 999 -D 5 -RT5 1"
STACK="-BK 1 -BT 5 -TR 1 -TT 0.8 -TA 1.0 -F 0.20 -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1 -C25 0.0 -ZM4D 1.2 -ZM4 -0.5"
FLAG="-TT 1.2 -a 8 -RPS 1 -RD 1"
TAG=$1; shift
EXE=$1; shift
BASE=$1; shift
if [ "$BASE" = "defaults" ]; then STACK=""; FLAG=""; fi
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
{ echo "EXE: $EXE"; echo "BASE: $BASE"; echo "OVERRIDES: $*"; } > $D/s_$TAG.cmd
rm -f $D/s_$TAG.root $D/s_${TAG}_hists.root
"$EXE" -m hybrid \
  -i $S/LSTNtuple_PU200RelVal_300evt.root \
  -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
  -o $D/s_$TAG.root $ANCHOR $CTL $STACK $FLAG "$@" > $D/s_$TAG.log 2>&1 \
  || { echo "RUN FAILED $TAG"; tail -30 $D/s_$TAG.log; exit 1; }
createPerfNumDenHists -i $D/s_$TAG.root -o $D/s_${TAG}_hists.root >> $D/s_$TAG.log 2>&1
python3 $S/fanout4/compose_attach/compare_ab.py --proto $D/s_${TAG}_hists.root \
  --base $S/prototype/base300_hists.root --json $D/s_$TAG.json > $D/s_agg_$TAG.txt 2>/dev/null
echo "DONE $TAG"
