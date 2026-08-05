#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/a11_ref"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1 && cmsenv > /dev/null 2>&1 && source setup.sh > /dev/null 2>&1
post () {
  TAG="$1"; BH="$2"
  createPerfNumDenHists -i "$P/r_${TAG}.root" -o "$P/r_${TAG}_hists.root" >> "$P/r_${TAG}.log" 2>&1
  python3 "$S/prototype/compare_ab.py" --proto "$P/r_${TAG}_hists.root" --base "$BH" \
    --json "$P/r_${TAG}.json" > "$P/r_agg_${TAG}.txt" 2>/dev/null
  echo "[a11] RECOVERED $TAG"
}
post A55     "$S/rebase_ref/rb_base300_hists.root"
post A60     "$S/rebase_ref/rb_base300_hists.root"
post A50D2   "$S/rebase_ref/rb_base300_hists.root"
post W_C_A60 "$S/fin_ref/fin_base977_hists.root"
echo "[a11] RECOVERY COMPLETE"
