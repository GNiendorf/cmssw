#!/bin/bash
# A/B driver for the chain-tracking prototype tuning loop (plan 10.5).
#
# Usage: run_ab.sh <tag> [chainproto hybrid args...]
#   e.g. run_ab.sh theta15 -T 1.5 -e 0.2
#
# Runs chainproto -m hybrid over the 300-event PU200RelVal ntuple, produces
# createPerfNumDenHists output, and judges it against the frozen baseline
# (base300_hists.root) with compare_ab.py. Outputs (all in prototype/):
#   ab_<tag>.root        hybrid ntuple (impersonated LST ntuple)
#   ab_<tag>_hists.root  createPerfNumDenHists output
#   ab_<tag>.json        compare_ab.py metrics (machine-readable)
#   ab_<tag>.log         chainproto + harness logs
# Extra args after <tag> are passed through to chainproto
# (e.g. -e/-L/-T/-T4/-T5/-T6/-F/-P/-n; -T sets all three per-length thetaChain values).

set -u

if [ $# -lt 1 ]; then
  echo "Usage: $0 <tag> [chainproto hybrid args...]" >&2
  exit 1
fi
TAG="$1"
shift

STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
PROTO=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout2/c1_cell25
LSTNTUPLE="$STANDALONE/LSTNtuple_PU200RelVal_300evt.root"
TRKDIR=/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/
BASEHISTS=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype/base300_hists.root

OUT="$PROTO/ab_${TAG}.root"
HISTS="$PROTO/ab_${TAG}_hists.root"
JSON="$PROTO/ab_${TAG}.json"
LOG="$PROTO/ab_${TAG}.log"

# Environment (standing rule: pushd + double setup.sh + cmsenv).
pushd "$STANDALONE" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv
source setup.sh > /dev/null 2>&1

set -e
rm -f "$OUT" "$HISTS"

echo "[run_ab] tag=${TAG} chainproto args: $*"
echo "[run_ab] hybrid run -> $OUT (log: $LOG)"
"$PROTO/bin/chainproto" -m hybrid -i "$LSTNTUPLE" -t "$TRKDIR" -o "$OUT" "$@" > "$LOG" 2>&1
tail -8 "$LOG"

echo "[run_ab] createPerfNumDenHists -> $HISTS"
createPerfNumDenHists -i "$OUT" -o "$HISTS" >> "$LOG" 2>&1

echo "[run_ab] compare vs $BASEHISTS"
python3 /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype/compare_ab.py --proto "$HISTS" --base "$BASEHISTS" --json "$JSON"
