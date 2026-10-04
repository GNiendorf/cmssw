#!/bin/bash
# post.sh [final|partial]: val5k-r7 two-way ref (stock + #186 + NaN guard, trackingMkFitFit) vs target (MkFitAlpaka target menu, serial)
# over the ttbar files finished in BOTH arms, + tgpu (target on CUDA) vs ref on tgpu's files. Same recipe as r5_menu/val5k/scripts/post.sh:
# harvests harv/<arm>_ttbar.root, npz per file, sums check, paired tables (stats5.py: paired bootstrap over files), CSVs, MTV pages
# (two-way with ratio), TriggerResults, status, job times, npz identity (hltSeedsForMkFit = LST seeds: must be identical ref vs target).
MODE=${1:-final}
V=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka/r7_menu/val5k
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/pre2_4way
log() { echo "$(date '+%I:%M %p') $*" >> $V/PROGRESS.txt; }
s=ttbar
common() { for t in $(awk '{print $1}' /mnt/data1/gsn27/here/hltqcd/val5k/files_$s.txt | xargs -n1 basename | cut -c1-8); do ok=1
  for a in "$@"; do f=$V/runs/$a/$s/$t/DQM_$t.root; [ -s $f ] && [ $(stat -L -c %s $f) -gt 1000000 ] || ok=0; done; [ $ok = 1 ] && echo $t; done; }
tags=$(common ref target); echo "$tags" > $V/tags_$s.txt; n=$(echo $tags | wc -w)
gtags=$(common ref tgpu); echo "$gtags" > $V/tags_gpu_$s.txt; ng=$(echo $gtags | wc -w)
log "post($MODE): $s $n common ref/target files, $ng common ref/tgpu files"
source /cvmfs/cms.cern.ch/cmsset_default.sh; export SCRAM_ARCH=el9_amd64_gcc13; export TMPDIR=$V/tmp
mkdir -p $V/harv $V/tables $V/csv $V/web
harv() { O=$1; shift; H=$V/harv/$O; rm -rf $H; mkdir -p $H && cd $H || return 1
  ( cd $P/areas/v1cur/src && eval $(scramv1 runtime -sh) && cd $H && F=$(for f in "$@"; do echo -n "file:$f,"; done | sed 's/,$//') && \
    cmsDriver.py HARVEST -s HARVESTING:@trackingOnlyValidation+@trackingOnlyDQM+postProcessorHLTtrackingSequence+postProcessorHLTvertexing --conditions auto:phase2_realistic_T35 --filein $F --scenario pp --filetype DQM --mc -n -1 > harvest.log 2>&1 && \
    mv DQM_V0001_R000000001__Global__CMSSW_X_Y_Z__RECO.root ../$O.root && cd .. && rm -rf $H && echo "OK $O ($# files)" ) ; }
for a in ref target; do harv ${a}_$s $(for t in $tags; do echo $V/runs/$a/$s/$t/DQM_$t.root; done) > $V/logs/harv_${a}_$s.out 2>&1 & done
if [ $ng -gt 0 ]; then for a in ref tgpu; do harv ${a}_gpufiles_$s $(for t in $gtags; do echo $V/runs/$a/$s/$t/DQM_$t.root; done) > $V/logs/harv_${a}_gpufiles_$s.out 2>&1 & done; fi; wait
log "post($MODE): harvests $(cat $V/logs/harv_*_$s.out | tr '\n' ' ')"
cd $P/areas/v1cur/src && eval $(scramv1 runtime -sh) && cd $V || exit 1
for a in ref target tgpu; do mkdir -p $V/npz/$a/$s; for t in $(ls $V/runs/$a/$s 2>/dev/null); do f=$V/runs/$a/$s/$t/DQM_$t.root
  [ -s $f ] || continue; [ -s $V/npz/$a/$s/$t.npz ] && continue; python3 $P/scripts/extract.py $V/npz/$a/$s/$t.npz $f || echo "FAIL extract $a $t"; done; done > $V/logs/extract.out 2>&1
log "post($MODE): npz ref $(ls $V/npz/ref/$s | wc -l) target $(ls $V/npz/target/$s | wc -l) tgpu $(ls $V/npz/tgpu/$s | wc -l), extract fails $(grep -c FAIL $V/logs/extract.out)"
python3 - > $V/tables/sumcheck.txt 2>&1 <<'PY'
import numpy as np, uproot
V="/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka/r7_menu/val5k"
B="DQMData/Run 1/HLT/Run summary/Tracking/ValidationWRTtp/"; tags=open(f"{V}/tags_ttbar.txt").read().split(); bad=n=0
for a in ("ref","target"):
    h=uproot.open(f"{V}/harv/{a}_ttbar.root")
    for c in ("hltGeneral","hltInitialStepTrackSelectionHighPurity","hltInitialStep"):
        for k in ("num_assoc(simToReco)_pT","num_simul_pT","num_assoc(simToReco)_vertpos","num_simul_vertpos","num_reco_pT","num_assoc(recoToSim)_pT","num_duplicate_pT","num_reco_eta","num_duplicate_eta"):
            tot=sum(np.load(f"{V}/npz/{a}/ttbar/{t}.npz")[f"{c}|{k}"] for t in tags).astype(float); tot[1]+=tot[0]; tot[0]=0; tot[-2]+=tot[-1]; tot[-1]=0
            hv=h[B+c+"_hltAssociatorByHits/"+k].values(flow=True); n+=1
            if not np.array_equal(tot,hv): bad+=1; print("DIFF",a,c,k,tot.sum(),hv.sum())
print(f"{n} histograms checked, {bad} differ")
PY
log "post($MODE): sumcheck $(tail -n 1 $V/tables/sumcheck.txt)"
for c in hltGeneral hltInitialStepTrackSelectionHighPurity hltInitialStep hltSeedsForMkFit; do
  TAGS=$V/tags_$s.txt SUMMARY=1 PAIRS="1-0" python3 $P/scripts/stats5.py $s $c ref=$V/npz/ref target=$V/npz/target > $V/tables/two_${s}_$c.txt 2>&1
  [ $ng -gt 0 ] && TAGS=$V/tags_gpu_$s.txt SUMMARY=1 PAIRS="1-0" python3 $P/scripts/stats5.py $s $c ref=$V/npz/ref tgpu=$V/npz/tgpu > $V/tables/gpu_${s}_$c.txt 2>&1; done
log "post($MODE): tables done (tables/two_${s}_*.txt, gpu_${s}_*.txt)"
python3 $P/scripts/csv_export.py $V/csv $s stock186_mkFitFit=$V/harv/ref_$s.root MkFitAlpaka_target=$V/harv/target_$s.root > $V/logs/csv.out 2>&1
log "post($MODE): csv $(tail -n 2 $V/logs/csv.out | tr '\n' ' ')"
python3 $V/scripts/trig_sum.py $V/tags_$s.txt $V/runs/ref $V/runs/target ref target > $V/tables/trigger_$s.txt 2>&1
[ $ng -gt 0 ] && python3 $V/scripts/trig_sum.py $V/tags_gpu_$s.txt $V/runs/ref $V/runs/tgpu ref tgpu > $V/tables/trigger_gpu_$s.txt 2>&1
python3 $P/scripts/hist_ident.py $V/harv/ref_$s.root $V/harv/target_$s.root > $V/tables/hist_ident_$s.txt 2>&1
{ echo "status warnings per file (MkFitAlpakaStatus/Build/OutputWrapper LogWarnings in hlt.log; device modules in hlt.py):"
  for a in target tgpu; do for t in $(ls $V/runs/$a/$s 2>/dev/null); do echo "$a $t $(cat $V/runs/$a/$s/$t/status.txt 2>/dev/null)"; done; done
  echo "job wall time (16 threads, val5k concurrency, shared box: indicative)"
  for t in $tags; do echo "$t ref $(awk '{print $3}' $V/runs/ref/$s/$t/peak.txt) target $(awk '{print $3}' $V/runs/target/$s/$t/peak.txt) tgpu $(awk '{print $3}' $V/runs/tgpu/$s/$t/peak.txt 2>/dev/null)"; done; } > $V/tables/status_times_$s.txt
python3 $V/scripts/npz_cmp.py $V/npz/ref/$s $V/npz/target/$s $V/tags_$s.txt > $V/tables/target_vs_ref_npz_ident.txt 2>&1   # hltSeedsForMkFit (LST, untouched) must be identical
log "post($MODE): trigger $(head -n 1 $V/tables/trigger_$s.txt); status warnings total $(grep status_warnings $V/tables/status_times_$s.txt | awk '{s+=$4} END {print s+0}'); seeds identical $(grep hltSeedsForMkFit $V/tables/target_vs_ref_npz_ident.txt)"
cd $V/web; rm -rf val5k_$s; mkdir -p t; export TMPDIR=/proc/self/cwd/t   # short AF_UNIX socket path (lane path too long)
SM="ttbar PU200, CMSSW_20_1_0_pre2 + LST + #186 + #187 + Ofast MkFitCore + NaN guard, trackingMkFitFit, $n of 50 files"
python3 $P/scripts/mtv_labelled_col.py --extended --png --jobs 4 --html-sample "$SM" --labels "stock mkFit fit (+#186),MkFitAlpaka target" --colors "kBlack,kRed" \
  -o val5k_$s $V/harv/ref_$s.root $V/harv/target_$s.root > val5k_$s.log 2>&1
log "post($MODE): web val5k_$s rc=$? $(find val5k_$s -name '*.png' | wc -l) png"
log "POST DONE ($MODE)"
