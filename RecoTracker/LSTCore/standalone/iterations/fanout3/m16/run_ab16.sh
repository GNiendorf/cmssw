#!/bin/bash
# M16 A/B driver: identical to prototype/run_ab.sh but rooted in THIS directory
# (fanout3/m16) so the M16 fan-out never writes into the frozen prototype tree.
#
# Usage: run_ab16.sh <tag> [chainproto hybrid args...]
# Outputs (all in fanout3/m16/): ab_<tag>.root, ab_<tag>_hists.root,
#                                ab_<tag>.json, ab_<tag>.log

set -u

if [ $# -lt 1 ]; then
  echo "Usage: $0 <tag> [chainproto hybrid args...]" >&2
  exit 1
fi
TAG="$1"
shift

STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
PROTO="$STANDALONE/fanout3/m16"
LSTNTUPLE="$STANDALONE/LSTNtuple_PU200RelVal_300evt.root"
TRKDIR=/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/
BASEHISTS="$STANDALONE/prototype/base300_hists.root"

OUT="$PROTO/ab_${TAG}.root"
HISTS="$PROTO/ab_${TAG}_hists.root"
JSON="$PROTO/ab_${TAG}.json"
LOG="$PROTO/ab_${TAG}.log"

pushd "$STANDALONE" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv
source setup.sh > /dev/null 2>&1

set -e
rm -f "$OUT" "$HISTS"

echo "[run_ab16] tag=${TAG} chainproto args: $*"
echo "[run_ab16] hybrid run -> $OUT (log: $LOG)"
"$PROTO/bin/chainproto" -m hybrid -i "$LSTNTUPLE" -t "$TRKDIR" -o "$OUT" "$@" > "$LOG" 2>&1
tail -12 "$LOG"

echo "[run_ab16] createPerfNumDenHists -> $HISTS"
createPerfNumDenHists -i "$OUT" -o "$HISTS" >> "$LOG" 2>&1

echo "[run_ab16] compare vs $BASEHISTS"
python3 "$STANDALONE/prototype/compare_ab.py" --proto "$HISTS" --base "$BASEHISTS" --json "$JSON"
