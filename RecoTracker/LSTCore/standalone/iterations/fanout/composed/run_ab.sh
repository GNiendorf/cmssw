#!/bin/bash
# Composed (a2 gate + a8 arbitration) A/B driver. Outputs stay in fanout/composed.
set -u
if [ $# -lt 1 ]; then echo "Usage: $0 <tag> [args...]" >&2; exit 1; fi
TAG="$1"; shift
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
PROTO="$STANDALONE/prototype"
MINE="$STANDALONE/fanout/composed"
LSTNTUPLE="$STANDALONE/LSTNtuple_PU200RelVal_300evt.root"
TRKDIR=/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/
BASEHISTS="$PROTO/base300_hists.root"
OUT="$MINE/ab_${TAG}.root"; HISTS="$MINE/ab_${TAG}_hists.root"
JSON="$MINE/ab_${TAG}.json"; LOG="$MINE/ab_${TAG}.log"
pushd "$STANDALONE" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv
source setup.sh > /dev/null 2>&1
set -e
rm -f "$OUT" "$HISTS"
echo "[composed] tag=${TAG} args: $*"
"$MINE/bin/chainproto" -m hybrid -i "$LSTNTUPLE" -t "$TRKDIR" -o "$OUT" "$@" > "$LOG" 2>&1
createPerfNumDenHists -i "$OUT" -o "$HISTS" >> "$LOG" 2>&1
python3 "$PROTO/compare_ab.py" --proto "$HISTS" --base "$BASEHISTS" --json "$JSON" >> "$LOG" 2>&1
echo "[composed] done ${TAG}"
