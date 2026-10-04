#!/bin/bash
# timing_chain.sh: round-7 CI-shape timing, arms alternated across 3 GPU reps (S F T / T F S / S F T) and 2 CPU reps (+ the D7-c
# MALLOC_CONF measurement arm targetmc on CPU, rep 2). Waits until none of this lane's validation cmsRun jobs runs. STOP_TIMING stops it.
L=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka; M=$L/r7_menu/menu; T=$M/scripts/run_timing.sh
# (no wait: launched by hand after the validation waves)
echo "TIMING CHAIN START $(date '+%-I:%M:%S %p') load $(cut -d' ' -f1 /proc/loadavg)"
$T r1 stock_gpu stockfit_gpu target_gpu stock_cpu stockfit_cpu target_cpu
$T r2 target_gpu stockfit_gpu stock_gpu target_cpu targetmc_cpu stockfit_cpu stock_cpu
$T r3 stock_gpu stockfit_gpu target_gpu
echo "TIMING CHAIN DONE $(date '+%-I:%M:%S %p')"
