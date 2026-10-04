#!/bin/bash
# queue2.sh: the trackingMkFitFit menu (D5-c), production swap (device EventOfHits + device building; the STOCK MkFitFitProducer and
# MkFitOutputTrackConverter on a full host EventOfHits, because the device fit is not yet enabled in main) + an in-job stock reference
# chain (EOH, MkFitProducer, MkFitFitProducer, MkFitOutputTrackConverter) + comparators + status check (xc_mkfitfit.txt).
# Starts after queue.sh; two jobs at a time.
R=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka/r5_menu; S=$R/menu/scripts
log() { echo "$(date '+%I:%M %p') $*" >> $R/PROGRESS.txt; }
until grep -q "queue: all menu jobs done" $R/PROGRESS.txt; do sleep 20; done
export XC="$(cat $S/xc_mkfitfit.txt)" PROCMOD=trackingMkFitFit
log "queue2: trackingMkFitFit menu validation starts (CPU + GPU ttbar, then CPU + GPU qcd)"
for smp in ttbar qcd; do
  $S/menu_job2.sh mkfitfit_cpu_$smp port cpu $smp 500 customizeHLTforMkFitAlpaka > $R/menu/runs/mkfitfit_cpu_$smp.out 2>&1 &
  CUDA_VISIBLE_DEVICES=0 $S/menu_job2.sh mkfitfit_gpu_$smp port gpu-nvidia $smp 500 customizeHLTforMkFitAlpaka > $R/menu/runs/mkfitfit_gpu_$smp.out 2>&1 &
  wait
  log "queue2: $smp $(tail -n 1 $R/menu/runs/mkfitfit_cpu_$smp.out) $(tail -n 1 $R/menu/runs/mkfitfit_gpu_$smp.out)"
done
log "queue2: done"
