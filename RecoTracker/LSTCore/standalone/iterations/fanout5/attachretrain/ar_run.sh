#!/bin/bash
# ar_run.sh -- ATTACH-HEAD RETRAIN agent driver (fanout5/attachretrain).
# Usage: ar_run.sh <tag> <mode> [flag overrides...]
#   mode = "def"   -> ANCHOR + CTL only            (repro gate 1)
#          "stack" -> ANCHOR + CTL + STACK         (M19 stack)
#          "fl"    -> ANCHOR + CTL + STACK + FLAGSHIP overrides
# Per-run overrides are appended LAST so they win.
TAG="$1"; shift
MODE="$1"; shift
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout5/attachretrain"
ANCHOR="-e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9 -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2 -C25 2.0 -C25D -2.0"
CTL="-A 4 -a 999 -D 5 -RT5 1"
STACK="-BK 1 -BT 5 -TR 1 -TT 0.8 -TA 1.0 -F 0.20 -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1 -C25 0.0 -ZM4D 1.2 -ZM4 -0.5"
FLAG="-TT 1.2 -a 8 -RPS 1 -RD 1"

case "$MODE" in
  def)   EXTRA="" ;;
  stack) EXTRA="$STACK" ;;
  fl)    EXTRA="$STACK $FLAG" ;;
  *) echo "bad mode $MODE"; exit 1 ;;
esac

pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
rm -f "$P/ar_${TAG}.root" "$P/ar_${TAG}_hists.root"
echo "MODE=$MODE OVERRIDES: $*" > "$P/ar_${TAG}.cmd"
echo "[ar] $TAG ($MODE) : $*"
# AR_CM=1 arms the attach confusion-matrix instrument (PROTO_ATTACH_CM). It is
# diagnostic-only: it changes no decision and no output row, it just prints an
# [ATTACHCM] block after the summary. Left OFF for the repro gates so those runs are
# byte-identical to the golden ones.
[ "${AR_CM:-0}" = "1" ] && export PROTO_ATTACH_CM=1
# AR_BIN selects which head-specific binary to run. Each trained variant is exported,
# built and archived as bin/chainproto_<variant>, because attach_mlp_weights.h can only
# hold one head at a time and the variants must be compared in the SAME pipeline.
BIN="${AR_BIN:-$P/bin/chainproto}"
/usr/bin/time -f "WALL %e s" "$BIN" -m hybrid -i "$S/LSTNtuple_PU200RelVal_300evt.root" \
  -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
  -o "$P/ar_${TAG}.root" $ANCHOR $CTL $EXTRA "$@" > "$P/ar_${TAG}.log" 2>&1 \
  || { echo "RUN FAILED $TAG"; tail -30 "$P/ar_${TAG}.log"; exit 1; }
createPerfNumDenHists -i "$P/ar_${TAG}.root" -o "$P/ar_${TAG}_hists.root" >> "$P/ar_${TAG}.log" 2>&1
python3 "$S/prototype/compare_ab.py" --proto "$P/ar_${TAG}_hists.root" --base "$S/prototype/base300_hists.root" \
  --json "$P/ar_${TAG}.json" > "$P/ar_agg_${TAG}.txt" 2>/dev/null
echo "[ar] DONE $TAG"
