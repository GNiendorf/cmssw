#!/bin/bash
# menu_job.sh <tag> <stock|port> <acc> <sample> <nev> [customise func]: ONE-STEP HLT+DQM, the EXACT command line of
# lanes/pre2_4way/scripts/job.sh (75e33_timing, val5k recipe) with these lane changes only (as r4_menu/scripts/menu_job.sh):
#   --filein = ALL local RAW files of the sample in ls order (ttbar 5 x 100 ev, qcd 10 x 50 ev = the r3 floor500 samples);
#   --nThreads 8 (box rule; job.sh: 16); --accelerators cpu | gpu-nvidia;
#   area: AREA env (default: pre2_4way/areas/vA for stock, r5_menu/val5k/areas/port = vA + MkFitAlpaka(+Formats) for port);
#   port arm: --customise RecoTracker/MkFitAlpaka/customizeHLTforMkFitAlpaka.<func> (default customizeHLTforMkFitAlpaka);
#   PROCMOD env: --procModifiers (e.g. trackingMkFitFit, D5-c); CFGONLY=1: make hlt.py and stop.
#   XC env: extra customise_commands (as job.sh's cfg/<arm>.xc); PRELOAD_LIBDIR env: LD_LIBRARY_PATH prefix (v2 floor).
T=$1; A=$2; ACC=$3; SMP=$4; N=${5:--1}; FUNC=${6:-customizeHLTforMkFitAlpaka}
R=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/mkfit_alpaka/r5_menu
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/mem_ref/lanes/pre2_4way
D=$R/menu/runs/$T; mkdir -p $D && cd $D || exit 1
case $SMP in
  ttbar) IN=$(ls /mnt/data1/gsn27/here/ttbar_raw/*.root | sed 's|^|file:|' | paste -sd,);;
  qcd)   IN=$(ls /mnt/data1/gsn27/here/qcd_raw/*.root | sed 's|^|file:|' | paste -sd,);;
esac
source /cvmfs/cms.cern.ch/cmsset_default.sh; export SCRAM_ARCH=el9_amd64_gcc13; export TMPDIR=$R/tmp
if [ $A = stock ]; then AR=${AREA:-$P/areas/vA}; else AR=${AREA:-$R/val5k/areas/port}; fi
cd $AR/src && eval $(scramv1 runtime -sh) && cd $D || exit 1
[ -n "$PRELOAD_LIBDIR" ] && export LD_LIBRARY_PATH=$PRELOAD_LIBDIR:$LD_LIBRARY_PATH
CUS=(); [ $A != stock ] && CUS=(--customise RecoTracker/MkFitAlpaka/customizeHLTforMkFitAlpaka.$FUNC)
SETUP="--conditions auto:phase2_realistic_T35 --geometry ExtendedRun4D121 --era Phase2C22I13M9"
XC="${XC:-}"
echo "area $AR arm $A acc $ACC smp $SMP n $N func $FUNC preload ${PRELOAD_LIBDIR:-none} gpu ${CUDA_VISIBLE_DEVICES:-all}" > info.txt
cmsDriver.py step2 -s L1P2GT,HLT:75e33_timing,VALIDATION::hltMultiTrackValidation+hltMultiPVValidation --processName=HLTX --hltProcess HLTX $SETUP ${PROCMOD:+--procModifiers $PROCMOD} \
  --customise SLHCUpgradeSimulations/Configuration/aging.customise_aging_1000 "${CUS[@]}" --filein $IN \
  --inputCommands='keep *, drop *_hlt*_*_HLT, drop triggerTriggerFilterObjectWithRefs_l1t*_*_HLT' \
  --eventcontent DQM --datatier DQMIO --fileout file:DQM_$T.root \
  --customise_commands "process.hltTrackValidator.label += [\"hltInitialStepTracks\"]\nprocess.options.wantSummary = True\nprocess.hltSeedsForMkFit = process.hltInitialStepTrajectorySeedsLSTTracks.clone()\nprocess.HLTInitialStepSequence += process.hltSeedsForMkFit\nprocess.hltTrackValidator.label += [\"hltSeedsForMkFit\"]\n$XC\n" \
  --python_filename hlt.py --mc -n $N --nThreads 8 --accelerators $ACC --no_exec > hlt_cfg.log 2>&1 || { echo "FAIL-CFG $T"; exit 1; }
[ -n "$CFGONLY" ] && { echo "CFG $T"; exit 0; }
echo "start $(date '+%-I:%M:%S %p') load $(cut -d' ' -f1 /proc/loadavg)" > times.txt
/usr/bin/time -v cmsRun hlt.py > hlt.log 2>&1 & tp=$!; echo $tp > cmsrun.pid
( for i in $(seq 1 120); do c=$(pgrep -P $tp cmsRun | head -n 1); [ -n "$c" ] && grep -q libRecoTrackerMkFitCore /proc/$c/maps 2>/dev/null && { sleep 30; grep -oE '/[^ ]*(MkFit|mkfit)[^ ]*\.so' /proc/$c/maps | sort -u > libs.txt; break; }; sleep 5; done ) &
wait $tp; rc=$?
echo "end $(date '+%-I:%M:%S %p') rc=$rc load $(cut -d' ' -f1 /proc/loadavg)" >> times.txt
echo "hlt $(grep 'Maximum resident' hlt.log | awk '{print $NF}') $(grep 'Elapsed (wall' hlt.log | awk '{print $NF}')" > peak.txt
[ $rc -eq 0 ] && [ -s DQM_$T.root ] || { echo "FAIL-HLT $T rc=$rc"; exit 1; }
echo "OK $T"
