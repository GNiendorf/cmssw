#!/bin/bash
# cw_run.sh -- fanout4/cheapwins: NO-RETRAIN cheap-lever A/B on the ctl_noatt
# authentic-replacement config (M12 gate scale, the f2 anchor thresholds).
#   cw_run.sh <tag> [flag overrides...]
# Later flags win, so overrides just append.
TAG="$1"; shift
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout4/cheapwins"
ANCHOR="-e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9 -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2 -C25 2.0 -C25D -2.0"
CTL="-A 4 -a 999 -D 5 -RT5 1"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
rm -f "$P/ab_${TAG}.root" "$P/ab_${TAG}_hists.root"
echo "[cw] $TAG : $*"
"$P/bin/chainproto" -m hybrid -i "$S/LSTNtuple_PU200RelVal_300evt.root" \
  -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
  -o "$P/ab_${TAG}.root" $ANCHOR $CTL "$@" > "$P/ab_${TAG}.log" 2>&1 || { tail -20 "$P/ab_${TAG}.log"; exit 1; }
createPerfNumDenHists -i "$P/ab_${TAG}.root" -o "$P/ab_${TAG}_hists.root" >> "$P/ab_${TAG}.log" 2>&1
python3 "$S/prototype/compare_ab.py" --proto "$P/ab_${TAG}_hists.root" --base "$S/prototype/base300_hists.root" \
  --json "$P/ab_${TAG}.json" > "$P/agg_${TAG}.txt" 2>/dev/null
grep -E "chain TCs" "$P/ab_${TAG}.log"
python3 - "$P/ab_${TAG}.json" "$TAG" <<'EOF'
import json,sys
m=json.load(open(sys.argv[1]))['metrics']
g=lambda k: m[k]['proto']
print("%-14s eff %.4f | vxy %.4f/%.4f/%.4f/%.4f | dxy %.4f/%.4f/%.4f/%.4f | fake %.4f dup %.4f | TC/evt %.1f"%(
 sys.argv[2],
 g('eff_overall_incut'),g('eff_vxy_0_1'),g('eff_vxy_1_5'),g('eff_vxy_5_10'),g('eff_vxy_10_30'),
 g('eff_dxy_0_1'),g('eff_dxy_1_5'),g('eff_dxy_5_10'),g('eff_dxy_10_30'),
 g('fake_overall_incut'),g('dup_overall_incut'),
 g('n_tc')/300.0))
EOF
