#!/bin/bash
# oos_run.sh -- M18b Task C: same ANCHOR+CTL+STACK as at_run.sh but over the 498-event
# PU200RelVal ntuple (349 of whose events do NOT appear in the 300-event tuning file).
# No compare_ab here: the hists are built after the 349-event filter.
#   oos_run.sh <tag> [flag overrides...]
TAG="$1"; shift
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout4/compose_attach"
ANCHOR="-e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9 -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2 -C25 2.0 -C25D -2.0"
CTL="-A 4 -a 999 -D 5 -RT5 1"
STACK="-B 10 -BK 1 -BT 5 -TR 1 -TT 0.8 -TA 1.0 -F 0.20 -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1 -C25 0.0 -ZM4D 1.2 -ZM4 -0.5"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
rm -f "$P/oos_${TAG}.root"
echo "OVERRIDES: $*" > "$P/oos_${TAG}.cmd"
"$P/bin/chainproto" -m hybrid -i "$S/LSTNtuple_PU200RelVal_1000evt.root" \
  -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
  -o "$P/oos_${TAG}.root" $ANCHOR $CTL $STACK "$@" > "$P/oos_${TAG}.log" 2>&1 \
  || { echo "RUN FAILED $TAG"; tail -30 "$P/oos_${TAG}.log"; exit 1; }
echo "[oos] DONE $TAG"
