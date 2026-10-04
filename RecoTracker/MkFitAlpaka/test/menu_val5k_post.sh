#!/bin/bash
# post.sh [final|partial]: val5k two-way (stock vA vs MkFitAlpaka port) over the ttbar files finished in BOTH arms:
# harvests harv/{stock,port}_ttbar.root, npz (port; stock = pre2_4way npz/vA of the same DQM files), sums check, paired tables
# (stats5.py: paired bootstrap over files), CSVs, MTV pages web/val5k_ttbar (two-way with ratio), TriggerResults, status, job times;
# stockrep vs stock (reproducibility of the reused stock outputs).
MODE=${1:-final}
V=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka/r5_menu/val5k
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/pre2_4way
log() { echo "$(date '+%I:%M %p') $*" >> $V/PROGRESS.txt; }
s=ttbar
tags=$(for t in $(awk '{print $1}' /mnt/data1/gsn27/here/hltqcd/val5k/files_$s.txt | xargs -n1 basename | cut -c1-8); do ok=1
  for a in stock port; do f=$V/runs/$a/$s/$t/DQM_$t.root; [ -s $f ] && [ $(stat -L -c %s $f) -gt 1000000 ] || ok=0; done; [ $ok = 1 ] && echo $t; done)
echo "$tags" > $V/tags_$s.txt; n=$(echo $tags | wc -w); log "post($MODE): $s $n common files"
source /cvmfs/cms.cern.ch/cmsset_default.sh; export SCRAM_ARCH=el9_amd64_gcc13; export TMPDIR=$V/tmp
mkdir -p $V/harv $V/tables $V/csv $V/web
harv() { O=$1; shift; H=$V/harv/$O; rm -rf $H; mkdir -p $H && cd $H || return 1
  ( cd $P/areas/v1cur/src && eval $(scramv1 runtime -sh) && cd $H && F=$(for f in "$@"; do echo -n "file:$f,"; done | sed 's/,$//') && \
    cmsDriver.py HARVEST -s HARVESTING:@trackingOnlyValidation+@trackingOnlyDQM+postProcessorHLTtrackingSequence+postProcessorHLTvertexing --conditions auto:phase2_realistic_T35 --filein $F --scenario pp --filetype DQM --mc -n -1 > harvest.log 2>&1 && \
    mv DQM_V0001_R000000001__Global__CMSSW_X_Y_Z__RECO.root ../$O.root && cd .. && rm -rf $H && echo "OK $O ($# files)" ) ; }
for a in stock port; do harv ${a}_$s $(for t in $tags; do echo $V/runs/$a/$s/$t/DQM_$t.root; done) > $V/logs/harv_${a}_$s.out 2>&1 & done; wait
log "post($MODE): harvests $(cat $V/logs/harv_{stock,port}_$s.out | tr '\n' ' ')"
cd $P/areas/v1cur/src && eval $(scramv1 runtime -sh) && cd $V || exit 1
mkdir -p $V/npz/port/$s $V/npz/stockrep/$s
for a in port stockrep; do for t in $( [ $a = port ] && echo $tags || ls $V/runs/$a/$s 2>/dev/null); do  # port: only the complete common files f=$V/runs/$a/$s/$t/DQM_$t.root; [ -s $f ] || continue; [ -s $V/npz/$a/$s/$t.npz ] && continue
  python3 $P/scripts/extract.py $V/npz/$a/$s/$t.npz $f || echo "FAIL extract $a $t"; done; done > $V/logs/extract.out 2>&1
log "post($MODE): npz port $(ls $V/npz/port/$s | wc -l), extract fails $(grep -c FAIL $V/logs/extract.out)"
# sums check: per-file npz counts summed == merged harvest (both arms)
python3 - > $V/tables/sumcheck.txt 2>&1 <<'PY'
import numpy as np, uproot
V="/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka/r5_menu/val5k"
B="DQMData/Run 1/HLT/Run summary/Tracking/ValidationWRTtp/"; tags=open(f"{V}/tags_ttbar.txt").read().split(); bad=n=0
for a in ("stock","port"):
    h=uproot.open(f"{V}/harv/{a}_ttbar.root")
    for c in ("hltGeneral","hltInitialStepTrackSelectionHighPurity","hltInitialStep"):
        for k in ("num_assoc(simToReco)_pT","num_simul_pT","num_assoc(simToReco)_vertpos","num_simul_vertpos","num_reco_pT","num_assoc(recoToSim)_pT","num_duplicate_pT","num_reco_eta","num_duplicate_eta"):
            tot=sum(np.load(f"{V}/npz/{a}/ttbar/{t}.npz")[f"{c}|{k}"] for t in tags).astype(float); tot[1]+=tot[0]; tot[0]=0; tot[-2]+=tot[-1]; tot[-1]=0
            hv=h[B+c+"_hltAssociatorByHits/"+k].values(flow=True); n+=1
            if not np.array_equal(tot,hv): bad+=1; print("DIFF",a,c,k,tot.sum(),hv.sum())
print(f"{n} histograms checked, {bad} differ")
PY
log "post($MODE): sumcheck $(tail -n 1 $V/tables/sumcheck.txt)"
export TAGS=$V/tags_$s.txt
for c in hltGeneral hltInitialStepTrackSelectionHighPurity hltInitialStep hltSeedsForMkFit; do
  SUMMARY=1 PAIRS="1-0" python3 $P/scripts/stats5.py $s $c stock=$V/npz/stock port=$V/npz/port > $V/tables/two_${s}_$c.txt 2>&1; done
log "post($MODE): tables done (tables/two_${s}_*.txt)"
python3 $P/scripts/csv_export.py $V/csv $s stock_mkFit=$V/harv/stock_$s.root MkFitAlpaka_port=$V/harv/port_$s.root > $V/logs/csv.out 2>&1
log "post($MODE): csv $(tail -n 2 $V/logs/csv.out | tr '\n' ' ')"
python3 $V/scripts/trig_sum.py $V/tags_$s.txt $V/runs/stock $V/runs/port stock port > $V/tables/trigger_$s.txt 2>&1
python3 $P/scripts/hist_ident.py $V/harv/stock_$s.root $V/harv/port_$s.root > $V/tables/hist_ident_$s.txt 2>&1
{ echo "port status warnings per file (MkFitAlpakaStatus/Build/OutputWrapper LogWarnings in hlt.log; device modules in hlt.py):"
  for t in $tags; do echo "$t $(cat $V/runs/port/$s/$t/status.txt 2>/dev/null)"; done
  echo "job wall time (16 threads, val5k concurrency; stock = pre2_4way vA run, other jobs on the box differ: indicative)"
  for t in $tags; do echo "$t stock $(cat $V/runs/stock/$s/$t/peak.txt | awk '{print $3}') port $(cat $V/runs/port/$s/$t/peak.txt | awk '{print $3}')"; done; } > $V/tables/status_times_$s.txt
python3 $V/scripts/npz_cmp.py $V/npz/stock/$s $V/npz/stockrep/$s > $V/tables/stockrep_ident.txt 2>&1   # reproducibility of the reused stock outputs
python3 $V/scripts/npz_cmp.py $V/npz/stock/$s $V/npz/port/$s $V/tags_$s.txt > $V/tables/port_vs_stock_npz_ident.txt 2>&1   # clone-the-baseline: hltSeedsForMkFit (LST, untouched) must be identical
log "post($MODE): trigger $(head -n 1 $V/tables/trigger_$s.txt); status warnings total $(grep status_warnings $V/tables/status_times_$s.txt | awk '{s+=$3} END {print s+0}'); stockrep: $(head -n 1 $V/tables/stockrep_ident.txt); seeds identical $(grep hltSeedsForMkFit $V/tables/port_vs_stock_npz_ident.txt)"
cd $V/web; rm -rf val5k_$s; mkdir -p t; export TMPDIR=/proc/self/cwd/t   # short AF_UNIX socket path for multiprocessing (lane path too long; --jobs 1 crashes in html.py)
SM="ttbar PU200, CMSSW_20_1_0_pre2 + current LST (pre2_4way vA), $n of 50 files"
python3 $P/scripts/mtv_labelled_col.py --extended --png --jobs 4 --html-sample "$SM" --labels "stock mkFit,MkFitAlpaka port" --colors "kBlack,kRed" \
  -o val5k_$s $V/harv/stock_$s.root $V/harv/port_$s.root > val5k_$s.log 2>&1
log "post($MODE): web val5k_$s rc=$? $(find val5k_$s -name '*.png' | wc -l) png"
log "POST DONE ($MODE)"
