#!/bin/bash
# dc_score.sh <tag> -- hists + compare_ab for a finished dc_<tag>.root
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout4/dupcut"
TAG="$1"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
createPerfNumDenHists -i "$P/dc_${TAG}.root" -o "$P/dc_${TAG}_hists.root" > "$P/hists_${TAG}.log" 2>&1 || {
  tail -5 "$P/hists_${TAG}.log"; exit 1; }
python3 "$P/compare_ab.py" --proto "$P/dc_${TAG}_hists.root" --base "$S/prototype/base300_hists.root" \
  --json "$P/ab_${TAG}.json" > "$P/agg_${TAG}.txt" 2>/dev/null
grep -E "chain TCs" "$P/dc_${TAG}.log"
cat "$P/agg_${TAG}.txt"
