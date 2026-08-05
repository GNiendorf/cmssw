#!/bin/bash
# A4 fan-out A/B driver: same recipe as prototype/run_ab.sh but ALL outputs stay in the
# a4_welding fan-out dir; shared inputs (ntuple, tracking dir, baseline hists) read-only.
set -u
if [ $# -lt 1 ]; then
  echo "Usage: $0 <tag> [chainproto hybrid args...]" >&2
  exit 1
fi
TAG="$1"; shift

STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
MINE="$STANDALONE/fanout/a4_welding"
LSTNTUPLE="$STANDALONE/LSTNtuple_PU200RelVal_300evt.root"
TRKDIR=/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/
BASEHISTS="$STANDALONE/prototype/base300_hists.root"

OUT="$MINE/ab_${TAG}.root"
HISTS="$MINE/ab_${TAG}_hists.root"
JSON="$MINE/ab_${TAG}.json"
LOG="$MINE/ab_${TAG}.log"

pushd "$STANDALONE" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv
source setup.sh > /dev/null 2>&1

set -e
rm -f "$OUT" "$HISTS"
echo "[a4] tag=${TAG} args: $*"
"$MINE/bin/chainproto" -m hybrid -i "$LSTNTUPLE" -t "$TRKDIR" -o "$OUT" "$@" > "$LOG" 2>&1
tail -14 "$LOG"
createPerfNumDenHists -i "$OUT" -o "$HISTS" >> "$LOG" 2>&1
python3 "$STANDALONE/prototype/compare_ab.py" --proto "$HISTS" --base "$BASEHISTS" --json "$JSON"
