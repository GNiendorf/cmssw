#!/bin/bash
# M17 (third gate retrain) A/B driver: ctl_noatt AUTHENTIC REPLACEMENT mode
# (-A 4 -a 999 -RT5 1: every carried type-7 row dropped, attach never fires, the chain
# slice alone must re-deliver the pT5 class) on the RETRAINED M17 gate scale.
#
#   m17_ab.sh <tag> [flag overrides...]
#
# BASE = the equal-kill (b3_calibrate/m17_calibrate) image of the f2/ctl_noatt thresholds
# on the M17 gate scale. Later flags win, so overrides just append.
TAG="$1"; shift
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout3/m16"
BASE="-e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.7901 -M5 1e9 -M6 1e9 -M4D -1.6373 -MD 1e9 \
-MR -1.9887 -MRI 0.3948 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2 \
-C25 1.931 -C25D -2.7353"
REPL="-A 4 -a 999 -RT5 1"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
rm -f "$P/ab_${TAG}.root" "$P/ab_${TAG}_hists.root"
echo "[m17_ab] $TAG : $*"
"$P/bin/chainproto" -m hybrid -i "$S/LSTNtuple_PU200RelVal_300evt.root" \
  -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
  -o "$P/ab_${TAG}.root" $BASE $REPL "$@" > "$P/ab_${TAG}.log" 2>&1 || { tail -20 "$P/ab_${TAG}.log"; exit 1; }
createPerfNumDenHists -i "$P/ab_${TAG}.root" -o "$P/ab_${TAG}_hists.root" >> "$P/ab_${TAG}.log" 2>&1
python3 "$S/prototype/compare_ab.py" --proto "$P/ab_${TAG}_hists.root" --base "$S/prototype/base300_hists.root" \
  --json "$P/ab_${TAG}.json" > "$P/agg_${TAG}.txt" 2>/dev/null
python3 - "$P/ab_${TAG}.json" "$TAG" <<'EOF'
import json,sys
m=json.load(open(sys.argv[1]))['metrics']
g=lambda k: m[k]['proto']
print("%-14s eff %.4f | vxy %.4f/%.4f/%.4f/%.4f | dxy %.4f/%.4f/%.4f/%.4f | fake %.4f dup %.4f | len %+.3f/%+.3f/%+.3f | TC/evt %.1f"%(
 sys.argv[2],
 g('eff_overall_incut'),g('eff_vxy_0_1'),g('eff_vxy_1_5'),g('eff_vxy_5_10'),g('eff_vxy_10_30'),
 g('eff_dxy_0_1'),g('eff_dxy_1_5'),g('eff_dxy_5_10'),g('eff_dxy_10_30'),
 g('fake_overall_incut'),g('dup_overall_incut'),
 m['mean_nhitOT_barrel']['delta'],m['mean_nhitOT_transition']['delta'],m['mean_nhitOT_endcap']['delta'],
 g('n_tc')/300.0))
EOF
grep -E "chain funnel|output TCs/evt" "$P/ab_${TAG}.log"
