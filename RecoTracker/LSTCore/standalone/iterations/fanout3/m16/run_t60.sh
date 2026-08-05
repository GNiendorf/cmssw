#!/bin/bash
# Frozen test-60 evaluation of an M16 A/B output: filter ab_<tag>.root to the 60
# held-out event keys, re-histogram, judge against prototype/base60_hists.root.
#   run_t60.sh <tag>
TAG="$1"
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout3/m16"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
rm -f "$P/t60_${TAG}.root" "$P/t60_${TAG}_hists.root"
root -l -b -q "$S/prototype/m12_filter_evts.C(\"$P/ab_${TAG}.root\",\"$P/t60_${TAG}.root\",\"$S/prototype/m12_test60_evts.txt\")" > "$P/t60_${TAG}.log" 2>&1
createPerfNumDenHists -i "$P/t60_${TAG}.root" -o "$P/t60_${TAG}_hists.root" >> "$P/t60_${TAG}.log" 2>&1
python3 "$S/prototype/compare_ab.py" --proto "$P/t60_${TAG}_hists.root" --base "$S/prototype/base60_hists.root" \
  --json "$P/t60_${TAG}.json" > "$P/t60agg_${TAG}.txt" 2>/dev/null
python3 - "$P/t60_${TAG}.json" "$TAG" <<'EOF'
import json,sys
m=json.load(open(sys.argv[1]))['metrics']
g=lambda k,w='proto': m[k][w]
for w in ('proto','base'):
    print("T60 %-12s %-5s eff %.4f | vxy %.4f/%.4f/%.4f/%.4f | dxy %.4f/%.4f/%.4f/%.4f | fake %.4f dup %.4f"%(
     sys.argv[2],w,g('eff_overall_incut',w),g('eff_vxy_0_1',w),g('eff_vxy_1_5',w),g('eff_vxy_5_10',w),g('eff_vxy_10_30',w),
     g('eff_dxy_0_1',w),g('eff_dxy_1_5',w),g('eff_dxy_5_10',w),g('eff_dxy_10_30',w),
     g('fake_overall_incut',w),g('dup_overall_incut',w)))
EOF
