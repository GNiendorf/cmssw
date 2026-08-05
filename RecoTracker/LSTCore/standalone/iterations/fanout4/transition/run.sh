#!/bin/bash
# m16r_run.sh -- READ-ONLY recon ablations on the ctl_noatt replacement config.
# Runs the prototype binary only (no builds, no source edits); writes m16r_*.root/.log.
#   m16r_run.sh <tag> [extra chainproto args...]
TAG="$1"; shift
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout4/transition"
ANCHOR="-e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9 -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2 -C25 2.0 -C25D -2.0"
CTL="-A 4 -a 999 -D 5 -RT5 1"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
echo "[m16r] $TAG : $*"
"$P/bin/chainproto" -m hybrid -i "$S/LSTNtuple_PU200RelVal_300evt.root" \
  -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
  -o "$P/m16r_${TAG}.root" $ANCHOR $CTL "$@" > "$P/m16r_${TAG}.log" 2>&1 \
  || { tail -20 "$P/m16r_${TAG}.log"; exit 1; }
grep -E "chain TCs|pixel TCs kept" "$P/m16r_${TAG}.log"
python3 "$P/m16r_sims.py" "$P/m16r_${TAG}.root" "$TAG"
