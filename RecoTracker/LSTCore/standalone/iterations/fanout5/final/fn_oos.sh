#!/bin/bash
# fn_oos.sh -- TRUE OUT-OF-SAMPLE scoring on the 349 non-overlap events, EXACTLY the
# M18b Task C method: run over the 1000-event PU200RelVal tuple, filter the output tree to
# the frozen (run,lumi,evt) list m18b_oos349_evts.txt with m18b_filter_evts.C, histogram the
# filtered tree, and compare against base_oos349_hists.root (LST on the SAME 349 events).
#   BIN=<path>  fn_oos.sh <tag> [overrides...]
TAG="$1"; shift
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout5/final"
A="$S/fanout4/compose_attach"      # READ-ONLY: filter macro, event list, LST OOS baseline
EXE="${BIN:-$P/bin/chainproto}"
ANCHOR="-e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9 -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2 -C25 2.0 -C25D -2.0"
CTL="-A 4 -a 999 -D 5 -RT5 1"
STACK="-BK 1 -BT 5 -TR 1 -TT 0.8 -TA 1.0 -F 0.20 -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1 -C25 0.0 -ZM4D 1.2 -ZM4 -0.5"
FLAGSHIP="$STACK -TT 1.2 -a 8 -RPS 1 -RD 1"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
rm -f "$P/oos_${TAG}.root" "$P/oos_${TAG}_349.root" "$P/oos_${TAG}_349_hists.root"
echo "BIN=$EXE OVERRIDES: $*" > "$P/oos_${TAG}.cmd"
echo "[oos] $TAG : $*"
"$EXE" -m hybrid -i "$S/LSTNtuple_PU200RelVal_1000evt.root" \
  -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
  -o "$P/oos_${TAG}.root" $ANCHOR $CTL $FLAGSHIP "$@" > "$P/oos_${TAG}.log" 2>&1 \
  || { echo "OOS RUN FAILED $TAG"; tail -30 "$P/oos_${TAG}.log"; exit 1; }
root -l -b -q "$A/m18b_filter_evts.C(\"$P/oos_${TAG}.root\",\"$P/oos_${TAG}_349.root\",\"$A/m18b_oos349_evts.txt\")" \
  >> "$P/oos_${TAG}.log" 2>&1
createPerfNumDenHists -i "$P/oos_${TAG}_349.root" -o "$P/oos_${TAG}_349_hists.root" >> "$P/oos_${TAG}.log" 2>&1
python3 "$S/prototype/compare_ab.py" --proto "$P/oos_${TAG}_349_hists.root" \
  --base "$A/base_oos349_hists.root" --json "$P/oos_${TAG}_349.json" > "$P/oos_agg_${TAG}.txt" 2>/dev/null
grep -E "matched|copied|keeping" "$P/oos_${TAG}.log" | tail -3
echo "[oos] DONE $TAG"
