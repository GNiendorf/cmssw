#!/bin/bash
# run_timing.sh <rep list> : CI-shape (4 jobs x 16 threads x 16 streams, 300 events after 50) tracking-time table arms,
# alternated: odd reps S F T, even reps T F S, GPU (L40) then CPU. S = stock default menu (KF fit; r6_base = the new
# reference), F = stock + #186 trackingMkFitFit menu (r6_base), T = our target menu (r6_menu area). STOP_TIMING file stops.
L=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka; R=$L/r6_menu/menu
B=$R/scripts/bench.sh; C=$R/timing/cfg; O=$R/timing/out
declare -A AREA=([stock]=$L/r6_base/CMSSW [stockfit]=$L/r6_base/CMSSW [target]=$L/r6_menu/CMSSW)
for i in $@; do
  if [ $((i % 2)) -eq 1 ]; then arms="stock stockfit target"; else arms="target stockfit stock"; fi
  for acc in gpu cpu; do
    for arm in $arms; do
      [ -f $R/STOP_TIMING ] && { echo "STOPPED $(date '+%-I:%M %p')"; exit 0; }
      echo "$arm $acc $i start $(date '+%-I:%M:%S %p') load $(cut -d' ' -f1 /proc/loadavg)"
      CUDA_VISIBLE_DEVICES=0 $B ${AREA[$arm]} $C/${arm}_${acc}.py $O/${arm}_${acc}_$i 4 16 16 300 50
    done
  done
done
echo "TIMING DONE $(date '+%-I:%M %p')"
