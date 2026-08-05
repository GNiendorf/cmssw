#!/bin/bash
# Angle B2 A/B driver (isolated copy; writes only into fanout2/b2_terminaltrim).
set -u
TAG="$1"; shift
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
D=$STANDALONE/fanout2/b2_terminaltrim
LSTNTUPLE="$STANDALONE/LSTNtuple_PU200RelVal_300evt.root"
TRKDIR=/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/
BASEHISTS="$STANDALONE/prototype/base300_hists.root"
OUT="$D/ab_${TAG}.root"; HISTS="$D/ab_${TAG}_hists.root"
JSON="$D/ab_${TAG}.json"; LOG="$D/ab_${TAG}.log"
set -e
rm -f "$OUT" "$HISTS"
"$D/bin/chainproto" -m hybrid -i "$LSTNTUPLE" -t "$TRKDIR" -o "$OUT" "$@" > "$LOG" 2>&1
grep -E "chain funnel|chain TCs|terminal trim" "$LOG"
createPerfNumDenHists -i "$OUT" -o "$HISTS" >> "$LOG" 2>&1
python3 "$STANDALONE/prototype/compare_ab.py" --proto "$HISTS" --base "$BASEHISTS" --json "$JSON" > /dev/null
python3 - "$JSON" <<'EOF'
import json,sys
m=json.load(open(sys.argv[1]))["metrics"]
k=["eff_overall_incut","eff_vxy_0_1","eff_vxy_1_5","eff_vxy_5_10","eff_vxy_10_30",
   "eff_dxy_0_1","eff_dxy_1_5","eff_dxy_5_10","eff_dxy_10_30","eff_barrel","eff_transition","eff_endcap",
   "fake_overall_incut","dup_overall_incut","mean_nhitOT_barrel","mean_nhitOT_transition","mean_nhitOT_endcap","n_tc"]
print(" ".join("%s=%.4f(%+.4f)"%(x.replace("eff_","e").replace("mean_nhitOT_","L").replace("_incut",""),m[x]["proto"],m[x]["delta"]) for x in k))
EOF
