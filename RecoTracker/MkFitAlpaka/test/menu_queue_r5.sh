#!/bin/bash
# queue.sh: the round-5 menu validation (production configuration = customizeHLTforMkFitAlpakaValidation: production swap + stock
# chain in the same job + MkFitAlpakaStatusCheck requireClean) in the val5k port area (vA + MkFitAlpaka main heads), on the floor500
# samples (all local RAW: ttbar 500, QCD 500); then the same validation with the four stock mkFit libraries switched to x86-64-v2.
# Two jobs at a time (lane rule), 8 threads each. Starts when the port-area build has finished with 0 errors.
R=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka/r5_menu
J=$R/menu/scripts/menu_job.sh; V2=$R/../r3_harness/floor500q/v2mkfit
log() { echo "$(date '+%I:%M %p') $*" >> $R/PROGRESS.txt; }
export XC="process.menuStatus = cms.EDAnalyzer('MkFitAlpakaStatusCheck', src=cms.InputTag('hltInitialStepTrackCandidatesMkFitDevice'), requireClean=cms.bool(True), summaryFile=cms.string('status.json'))\nprocess.hltMkFitAlpakaValidationSequence += process.menuStatus"
until grep -q "^errors:" $R/val5k/logs/build_port.log; do sleep 20; done
grep -q "^errors: 0 " $R/val5k/logs/build_port.log || { log "queue: port-area build has errors, not starting"; exit 1; }
until [ -s $R/menu/runs/stockgpu1_ttbar/DQM_stockgpu1_ttbar.root ] && [ -s $R/menu/runs/stockgpu2_ttbar/DQM_stockgpu2_ttbar.root ]; do sleep 20; done
log "queue: port area built; menu validation starts (slot A: CPU qcd, v2 CPU qcd; slot B: CPU ttbar, GPU ttbar, GPU qcd, v2 CPU ttbar)"
( $J portval_cpu_qcd port cpu qcd 500 customizeHLTforMkFitAlpakaValidation > $R/menu/runs/portval_cpu_qcd.out 2>&1
  log "queue A: portval_cpu_qcd $(tail -n 1 $R/menu/runs/portval_cpu_qcd.out)"
  PRELOAD_LIBDIR=$V2 $J portv2_cpu_qcd port cpu qcd 500 customizeHLTforMkFitAlpakaValidation > $R/menu/runs/portv2_cpu_qcd.out 2>&1
  log "queue A: portv2_cpu_qcd $(tail -n 1 $R/menu/runs/portv2_cpu_qcd.out)" ) &
( $J portval_cpu_ttbar port cpu ttbar 500 customizeHLTforMkFitAlpakaValidation > $R/menu/runs/portval_cpu_ttbar.out 2>&1
  log "queue B: portval_cpu_ttbar $(tail -n 1 $R/menu/runs/portval_cpu_ttbar.out)"
  CUDA_VISIBLE_DEVICES=0 $J portval_gpu_ttbar port gpu-nvidia ttbar 500 customizeHLTforMkFitAlpakaValidation > $R/menu/runs/portval_gpu_ttbar.out 2>&1
  log "queue B: portval_gpu_ttbar $(tail -n 1 $R/menu/runs/portval_gpu_ttbar.out)"
  CUDA_VISIBLE_DEVICES=0 $J portval_gpu_qcd port gpu-nvidia qcd 500 customizeHLTforMkFitAlpakaValidation > $R/menu/runs/portval_gpu_qcd.out 2>&1
  log "queue B: portval_gpu_qcd $(tail -n 1 $R/menu/runs/portval_gpu_qcd.out)"
  PRELOAD_LIBDIR=$V2 $J portv2_cpu_ttbar port cpu ttbar 500 customizeHLTforMkFitAlpakaValidation > $R/menu/runs/portv2_cpu_ttbar.out 2>&1
  log "queue B: portv2_cpu_ttbar $(tail -n 1 $R/menu/runs/portv2_cpu_ttbar.out)" ) &
wait
log "queue: all menu jobs done"
