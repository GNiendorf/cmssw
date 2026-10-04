#!/bin/bash
# wave.sh <name>: round-7 integrated-head validation jobs (2 concurrent per lane, 8 threads each)
# area = r7_menu/CMSSW = main6 head 6f88f2e + MkFitCore d1-nan-guards (D7-a reference in the in-job stock chain)
L=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka
J=$L/r7_menu/menu/scripts/menu_job.sh; A=$L/r7_menu/CMSSW; RUNS=$L/r7_menu/menu/runs; TV=customizeHLTforMkFitAlpakaTargetValidation
case $1 in
ttbar) $J tv_cpu_ttbar $A cpu ttbar 500 $TV > $RUNS/tv_cpu_ttbar.out 2>&1 &
       GPUDEV=0 $J tv_gpu_ttbar $A gpu-nvidia ttbar 500 $TV > $RUNS/tv_gpu_ttbar.out 2>&1 &
       wait;;
qcd)   $J tv_cpu_qcd $A cpu qcd 500 $TV > $RUNS/tv_cpu_qcd.out 2>&1 &
       GPUDEV=0 $J tv_gpu_qcd $A gpu-nvidia qcd 500 $TV > $RUNS/tv_gpu_qcd.out 2>&1 &
       wait;;
ref)   # stock + #186 (+ NaN guard) trackingMkFitFit, CPU, same events (MTV / trigger / DQM reference of the target runs)
       $J sf_cpu_ttbar $A cpu ttbar 500 > $RUNS/sf_cpu_ttbar.out 2>&1 &
       $J sf_cpu_qcd $A cpu qcd 500 > $RUNS/sf_cpu_qcd.out 2>&1 &
       wait;;
esac
echo "WAVE $1 DONE $(date '+%-I:%M %p')"
