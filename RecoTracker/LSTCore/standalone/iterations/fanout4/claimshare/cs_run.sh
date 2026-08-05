#!/bin/bash
# cs_run.sh -- claimshare fan-out runner: prototype run + hists + A/B scoreboard.
#   cs_run.sh <tag> [extra chainproto args...]
# env BIN overrides the binary (default bin/chainproto).
TAG="$1"; shift
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout4/claimshare"
BIN="${BIN:-$P/bin/chainproto}"
ANCHOR="-e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9 -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2 -C25 2.0 -C25D -2.0"
CTL="-A 4 -a 999 -D 5 -RT5 1"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
echo "[cs] $TAG : $*"
"$BIN" -m hybrid -i "$S/LSTNtuple_PU200RelVal_300evt.root" \
  -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
  -o "$P/cs_${TAG}.root" $ANCHOR $CTL "$@" > "$P/cs_${TAG}.log" 2>&1 \
  || { tail -20 "$P/cs_${TAG}.log"; exit 1; }
grep -E "chain TCs|pixel TCs kept|post-arb dedup" "$P/cs_${TAG}.log"
createPerfNumDenHists -i "$P/cs_${TAG}.root" -o "$P/cs_${TAG}_hists.root" > "$P/cs_${TAG}_hists.log" 2>&1 \
  || { tail -20 "$P/cs_${TAG}_hists.log"; exit 1; }
python3 "$P/compare_ab.py" --proto "$P/cs_${TAG}_hists.root" \
  --base "$S/prototype/base300_hists.root" --json "$P/cs_${TAG}.json" \
  > "$P/agg_${TAG}.txt" 2>&1
cat "$P/agg_${TAG}.txt"
