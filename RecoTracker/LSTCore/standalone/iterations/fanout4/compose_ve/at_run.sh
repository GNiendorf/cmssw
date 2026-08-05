#!/bin/bash
# at_run.sh -- ATTACH-CONTENTION agent driver (fanout4/compose_attach).
# Same anchor + CTL as every M17 sibling workspace. STACK is the provisional stack
# declared by the orchestrator; it is appended AFTER the CTL so its flags win, and
# per-run overrides are appended last so THEY win.
#   at_run.sh <tag> [flag overrides...]
TAG="$1"; shift
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout4/compose_ve"
ANCHOR="-e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9 -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2 -C25 2.0 -C25D -2.0"
CTL="-A 4 -a 999 -D 5 -RT5 1"
STACK="-B 10 -BK 1 -BT 5 -TR 1 -TT 0.8 -TA 1.0 -F 0.20 -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1 -C25 0.0 -ZM4D 1.2 -ZM4 -0.5"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
rm -f "$P/at_${TAG}.root" "$P/at_${TAG}_hists.root"
echo "[at] $TAG : $*"
echo "OVERRIDES: $*" > "$P/at_${TAG}.cmd"
"$P/bin/chainproto" -m hybrid -i "$S/LSTNtuple_PU200RelVal_300evt.root" \
  -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
  -o "$P/at_${TAG}.root" $ANCHOR $CTL $STACK "$@" > "$P/at_${TAG}.log" 2>&1 \
  || { echo "RUN FAILED $TAG"; tail -30 "$P/at_${TAG}.log"; exit 1; }
createPerfNumDenHists -i "$P/at_${TAG}.root" -o "$P/at_${TAG}_hists.root" >> "$P/at_${TAG}.log" 2>&1
python3 "$S/prototype/compare_ab.py" --proto "$P/at_${TAG}_hists.root" --base "$S/prototype/base300_hists.root" \
  --json "$P/at_${TAG}.json" > "$P/at_agg_${TAG}.txt" 2>/dev/null
echo "[at] DONE $TAG"
