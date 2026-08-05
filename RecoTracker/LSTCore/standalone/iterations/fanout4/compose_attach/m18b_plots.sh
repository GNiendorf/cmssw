#!/bin/bash
# M18b Task D: standard comparison curve set, LST baseline vs the flagship composition.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
A=$S/fanout4/compose_attach
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
BASE=$S/prototype/base300_hists.root
FLAG=$A/at_fl_balanced_hists.root
for v in pt eta vxy dxy; do
  python3 $S/efficiency/python/lst_plot_performance.py "$BASE" "$FLAG" \
    -L LST,ChainTracking -t m18b_flagship --compare -m eff -o TC -v $v
done
for v in pt eta; do
  python3 $S/efficiency/python/lst_plot_performance.py "$BASE" "$FLAG" \
    -L LST,ChainTracking -t m18b_flagship --compare -m fakerate -o TC -v $v
  python3 $S/efficiency/python/lst_plot_performance.py "$BASE" "$FLAG" \
    -L LST,ChainTracking -t m18b_flagship --compare -m duplrate -o TC -v $v
done
echo M18B_PLOTS_DONE
