#!/bin/bash
# fn_run.sh -- M19 FINAL COMPOSITION runner.
#   BIN=golden|local   BASE=defaults|flagship   fn_run.sh <tag> [overrides...]
# Overrides are appended LAST so they win. Default BIN=local, BASE=flagship.
TAG="$1"; shift
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout5/final"
if [ "$BIN" = "golden" ]; then EXE="$S/fanout4/compose_attach/bin/chainproto"
elif [ -n "$BIN" ] && [ "$BIN" != "local" ]; then EXE="$BIN"
else EXE="$P/bin/chainproto"; fi
ANCHOR="-e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9 -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2 -C25 2.0 -C25D -2.0"
CTL="-A 4 -a 999 -D 5 -RT5 1"
STACK="-BK 1 -BT 5 -TR 1 -TT 0.8 -TA 1.0 -F 0.20 -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1 -C25 0.0 -ZM4D 1.2 -ZM4 -0.5"
FLAGSHIP="$STACK -TT 1.2 -a 8 -RPS 1 -RD 1"
if [ "$BASE" = "defaults" ]; then STK=""; else STK="$FLAGSHIP"; fi
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
rm -f "$P/f_${TAG}.root" "$P/f_${TAG}_hists.root"
echo "BIN=$EXE BASE=${BASE:-flagship} OVERRIDES: $*" > "$P/f_${TAG}.cmd"
echo "[f] $TAG : $*"
"$EXE" -m hybrid -i "$S/LSTNtuple_PU200RelVal_300evt.root" \
  -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
  -o "$P/f_${TAG}.root" $ANCHOR $CTL $STK "$@" > "$P/f_${TAG}.log" 2>&1 \
  || { echo "RUN FAILED $TAG"; tail -30 "$P/f_${TAG}.log"; exit 1; }
createPerfNumDenHists -i "$P/f_${TAG}.root" -o "$P/f_${TAG}_hists.root" >> "$P/f_${TAG}.log" 2>&1
python3 "$S/prototype/compare_ab.py" --proto "$P/f_${TAG}_hists.root" --base "$S/prototype/base300_hists.root" \
  --json "$P/f_${TAG}.json" > "$P/f_agg_${TAG}.txt" 2>/dev/null
echo "[f] DONE $TAG"
