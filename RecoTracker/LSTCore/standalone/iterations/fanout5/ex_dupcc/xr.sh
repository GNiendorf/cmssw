#!/bin/bash
# xr.sh -- EXPLOIT ex_dupcc runner.
#   BIN=golden|local  xr.sh <tag> [overrides...]
# Default BIN=local (this dir's build). Golden tree is never written to.
TAG="$1"; shift
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout5/ex_dupcc"
if [ "$BIN" = "golden" ]; then EXE="$S/fanout4/compose_attach/bin/chainproto"; else EXE="$P/bin/chainproto"; fi
ANCHOR="-e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9 -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2 -C25 2.0 -C25D -2.0"
CTL="-A 4 -a 999 -D 5 -RT5 1"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
rm -f "$P/x_${TAG}.root" "$P/x_${TAG}_hists.root"
echo "BIN=$EXE OVERRIDES: $*" > "$P/x_${TAG}.cmd"
echo "[x] $TAG : $*"
"$EXE" -m hybrid -i "$S/LSTNtuple_PU200RelVal_300evt.root" \
  -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
  -o "$P/x_${TAG}.root" $ANCHOR $CTL "$@" > "$P/x_${TAG}.log" 2>&1 \
  || { echo "RUN FAILED $TAG"; tail -30 "$P/x_${TAG}.log"; exit 1; }
createPerfNumDenHists -i "$P/x_${TAG}.root" -o "$P/x_${TAG}_hists.root" >> "$P/x_${TAG}.log" 2>&1
python3 "$S/prototype/compare_ab.py" --proto "$P/x_${TAG}_hists.root" --base "$S/prototype/base300_hists.root" \
  --json "$P/x_${TAG}.json" > "$P/x_agg_${TAG}.txt" 2>/dev/null
echo "[x] DONE $TAG"
