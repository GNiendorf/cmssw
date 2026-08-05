#!/bin/bash
# M18b Task C step 4: the TRUE out-of-sample scoreboard.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
A=$S/fanout4/compose_attach
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
python3 $S/prototype/compare_ab.py --proto $A/oos_balanced_349_hists.root \
  --base $A/base_oos349_hists.root --json $A/oos_balanced_349.json > $A/oos_agg_349.txt 2>/dev/null
python3 - <<'PY'
import json
S="/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
m=json.load(open(S+"/fanout4/compose_attach/oos_balanced_349.json"))['metrics']
for who in ("base","proto"):
    g=lambda k: m[k][who]
    lbl="LST (OOS349)" if who=="base" else "fl_balanced (OOS349)"
    print("%-22s eff %.4f | vxy %.4f/%.4f/%.4f/%.4f | dxy %.4f/%.4f/%.4f/%.4f | fake %.4f dup %.4f | TC/evt %.1f"%(
      lbl,g('eff_overall_incut'),g('eff_vxy_0_1'),g('eff_vxy_1_5'),g('eff_vxy_5_10'),g('eff_vxy_10_30'),
      g('eff_dxy_0_1'),g('eff_dxy_1_5'),g('eff_dxy_5_10'),g('eff_dxy_10_30'),
      g('fake_overall_incut'),g('dup_overall_incut'),g('n_tc')/349.0))
print()
for k in ('eff_overall_incut','eff_vxy_0_1','eff_vxy_1_5','eff_vxy_5_10','eff_vxy_10_30',
          'eff_dxy_1_5','eff_dxy_5_10','fake_overall_incut','dup_overall_incut',
          'mean_nhitOT_barrel','mean_nhitOT_transition','mean_nhitOT_endcap'):
    if k in m:
        print("  %-22s proto %.4f  base %.4f  delta %+.4f"%(k,m[k]['proto'],m[k]['base'],m[k]['delta']))
PY
echo OOS_TABLE_DONE
