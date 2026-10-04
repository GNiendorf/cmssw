#!/bin/bash
# run_timing.sh <tag> <arm_acc> [...]: CI-shape (4 jobs x 16 threads x 16 streams, 300 events after 50) arms, sequential (round 7).
# stock = r6_base default KF menu, stockfit = r6_base + trackingMkFitFit (#186), target = r7_menu area (main6 head 6f88f2e + NaN-guard
# MkFitCore; the target menu does not run stock MkFitCore code except the light EventOfHits). cfgs = verify6_int's (same command lines).
# MALLOC=1 in the env of an arm name 'targetmc': target with MALLOC_CONF=oversize_threshold:0 (D7-c measurement arm, CPU only).
L=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka
R=$L/r7_menu/menu; B=$R/scripts/bench.sh; C=$R/timing/cfg; O=$R/timing/out
declare -A AREA=([stock]=$L/r6_base/CMSSW [stockfit]=$L/r6_base/CMSSW [target]=$L/r7_menu/CMSSW [targetmc]=$L/r7_menu/CMSSW)
tag=$1; shift
for aa in "$@"; do
  arm=${aa%_*}; acc=${aa##*_}; cfg=$arm; [ $arm = targetmc ] && cfg=target
  [ -f $R/STOP_TIMING ] && { echo "STOPPED $(date '+%-I:%M %p')"; exit 0; }
  echo "$arm $acc $tag start $(date '+%-I:%M:%S %p') load $(cut -d' ' -f1 /proc/loadavg) cmsRun on box $(pgrep -c -x cmsRun)"
  if [ $arm = targetmc ]; then MALLOC_CONF=oversize_threshold:0 GPUMEM=$([ $acc = gpu ] && echo 1) CUDA_VISIBLE_DEVICES=0 $B ${AREA[$arm]} $C/${cfg}_${acc}.py $O/${arm}_${acc}_$tag 4 16 16 300 50
  else GPUMEM=$([ $acc = gpu ] && echo 1) CUDA_VISIBLE_DEVICES=0 $B ${AREA[$arm]} $C/${cfg}_${acc}.py $O/${arm}_${acc}_$tag 4 16 16 300 50; fi
done
echo "TIMING $tag DONE $(date '+%-I:%M %p')"
