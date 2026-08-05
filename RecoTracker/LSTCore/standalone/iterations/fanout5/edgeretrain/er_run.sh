#!/bin/bash
# er_run.sh -- EDGE-HEAD DISPLACEMENT RETRAIN agent driver (fanout5/edgeretrain).
#   er_run.sh <tag> [flag overrides...]
# ANCHOR + CTL always applied. STACK only if ER_STACK=1 (default 1).
TAG="$1"; shift
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout5/edgeretrain"
ANCHOR="-e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9 -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2 -C25 2.0 -C25D -2.0"
CTL="-A 4 -a 999 -D 5 -RT5 1"
STACK="-BK 1 -BT 5 -TR 1 -TT 0.8 -TA 1.0 -F 0.20 -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1 -C25 0.0 -ZM4D 1.2 -ZM4 -0.5"
if [ "${ER_STACK:-1}" != "1" ]; then STACK=""; fi
BIN="${ER_BIN:-$P/bin/chainproto}"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
rm -f "$P/er_${TAG}.root" "$P/er_${TAG}_hists.root"
echo "[er] $TAG : bin=$BIN stack=${ER_STACK:-1} overrides: $*"
echo "BIN: $BIN ; STACK: $STACK ; OVERRIDES: $*" > "$P/er_${TAG}.cmd"
T_START=$(date +%s.%N)
"$BIN" -m hybrid -i "$S/LSTNtuple_PU200RelVal_300evt.root" \
  -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
  -o "$P/er_${TAG}.root" $ANCHOR $CTL $STACK "$@" > "$P/er_${TAG}.log" 2>&1 \
  || { echo "RUN FAILED $TAG"; tail -30 "$P/er_${TAG}.log"; exit 1; }
T_END=$(date +%s.%N)
python3 -c "print('WALL_MS_PER_EVT %.2f' % ((${T_END}-${T_START})*1000.0/300.0))" >> "$P/er_${TAG}.log"
createPerfNumDenHists -i "$P/er_${TAG}.root" -o "$P/er_${TAG}_hists.root" >> "$P/er_${TAG}.log" 2>&1
python3 "$P/compare_ab.py" --proto "$P/er_${TAG}_hists.root" --base "$S/prototype/base300_hists.root" \
  --json "$P/er_${TAG}.json" > "$P/er_agg_${TAG}.txt" 2>/dev/null
echo "[er] DONE $TAG"
