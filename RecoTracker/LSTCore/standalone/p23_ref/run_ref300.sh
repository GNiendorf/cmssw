#!/bin/bash
# P2.3 gate (b) REFERENCE leg: the frozen-minus-attach prototype over the 300-event
# PU200RelVal ntuple, then the standard harness (createPerfNumDenHists + compare_ab.py
# vs prototype/base300_hists.root).  Produces the scoreboard the ported pipeline must
# reproduce row for row.
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
PROTO="$STANDALONE/prototype"
REF="$STANDALONE/p23_ref"
LSTNTUPLE="$STANDALONE/LSTNtuple_PU200RelVal_300evt.root"
TRKDIR=/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/

source "$REF/frozen_flags.sh"

TAG="${1:-p23ref300}"
NEVT="${2:-}"
OUT="$REF/${TAG}.root"
HISTS="$REF/${TAG}_hists.root"
JSON="$REF/${TAG}.json"
LOG="$REF/${TAG}.log"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

set -e
rm -f "$OUT" "$HISTS"
EXTRA=()
if [ -n "$NEVT" ]; then EXTRA=(-n "$NEVT"); fi

echo "[p23ref] chainproto -> $OUT"
"$PROTO/bin/chainproto" -m hybrid -i "$LSTNTUPLE" -t "$TRKDIR" -o "$OUT" \
  "${FROZEN_FLAGS[@]}" "${EXTRA[@]}" > "$LOG" 2>&1
tail -20 "$LOG"

echo "[p23ref] createPerfNumDenHists -> $HISTS"
createPerfNumDenHists -i "$OUT" -o "$HISTS" >> "$LOG" 2>&1

echo "[p23ref] compare_ab.py vs base300_hists.root"
python3 "$PROTO/compare_ab.py" --proto "$HISTS" --base "$PROTO/base300_hists.root" --json "$JSON"
