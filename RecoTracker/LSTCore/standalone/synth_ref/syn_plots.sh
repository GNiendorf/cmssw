#!/bin/bash
# syn_plots.sh <cand_hists> <tag>   -- LST vs candidate comparison plots on the FULL 977.
# LST side = fin_ref/fin_base977_hists.root (the 977 LST identity reference).
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
CAND="$1"; TAG="${2:-finishline}"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
python3 "$S/efficiency/python/lst_plot_performance.py" \
  "$S/fin_ref/fin_base977_hists.root" "$CAND" \
  -L LST,ChainFinal -t "$TAG" --compare
echo "[plots] wrote $S/plots/$TAG"
