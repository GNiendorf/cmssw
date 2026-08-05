#!/bin/bash
# rd_run.sh -- RECON A (dup window) driver. Runs the GOLDEN binary only; never rebuilds.
#   rd_run.sh <tag> [flag overrides...]
TAG="$1"; shift
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
G="$S/fanout4/compose_attach"
P="$S/fanout5/recon_dup"
ANCHOR="-e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9 -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2 -C25 2.0 -C25D -2.0"
CTL="-A 4 -a 999 -D 5 -RT5 1"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
rm -f "$P/rd_${TAG}.root" "$P/rd_${TAG}_hists.root"
echo "OVERRIDES: $*" > "$P/rd_${TAG}.cmd"
echo "[rd] $TAG : $*"
"$G/bin/chainproto" -m hybrid -i "$S/LSTNtuple_PU200RelVal_300evt.root" \
  -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
  -o "$P/rd_${TAG}.root" $ANCHOR $CTL "$@" > "$P/rd_${TAG}.log" 2>&1 \
  || { echo "RUN FAILED $TAG"; tail -30 "$P/rd_${TAG}.log"; exit 1; }
createPerfNumDenHists -i "$P/rd_${TAG}.root" -o "$P/rd_${TAG}_hists.root" >> "$P/rd_${TAG}.log" 2>&1
python3 "$S/prototype/compare_ab.py" --proto "$P/rd_${TAG}_hists.root" --base "$S/prototype/base300_hists.root" \
  --json "$P/rd_${TAG}.json" > "$P/rd_agg_${TAG}.txt" 2>/dev/null
echo "[rd] DONE $TAG"
