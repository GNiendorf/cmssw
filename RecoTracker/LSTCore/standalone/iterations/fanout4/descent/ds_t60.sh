#!/bin/bash
# ds_t60.sh -- frozen test-60 evaluation, reproducing fanout3/m16/run_t60.sh verbatim
# (only the tag prefix + workspace differ). Filters ds_<tag>.root to the 60 held-out
# event keys in prototype/m12_test60_evts.txt, re-histograms, judges vs base60_hists.
#   ds_t60.sh <tag>
TAG="$1"
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout4/descent"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
rm -f "$P/t60_${TAG}.root" "$P/t60_${TAG}_hists.root"
root -l -b -q "$S/prototype/m12_filter_evts.C(\"$P/ds_${TAG}.root\",\"$P/t60_${TAG}.root\",\"$S/prototype/m12_test60_evts.txt\")" > "$P/t60_${TAG}.log" 2>&1
createPerfNumDenHists -i "$P/t60_${TAG}.root" -o "$P/t60_${TAG}_hists.root" >> "$P/t60_${TAG}.log" 2>&1
python3 "$S/prototype/compare_ab.py" --proto "$P/t60_${TAG}_hists.root" --base "$S/prototype/base60_hists.root" \
  --json "$P/t60_${TAG}.json" > "$P/t60agg_${TAG}.txt" 2>/dev/null
echo "[t60] DONE $TAG"
