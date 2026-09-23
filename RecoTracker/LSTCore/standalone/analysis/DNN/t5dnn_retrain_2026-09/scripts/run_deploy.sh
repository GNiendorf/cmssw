#!/bin/bash
# Deployed run of one arm: run_deploy.sh <arm (bin/<arm>)> <e3000|e4000|jets|cube50|cube50_highPt>
# Takes one of the 4 box-wide slots (displaced_ref/fan/.slots) and waits for >= 100 GB free RAM.
ARM=$1; CTX=$2
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
T=$S/displaced_ref/t5dnn; O=$T/deploy
case "$CTX" in
  e[0-9]*) IN=/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/event_${CTX#e}.root; EX="-n 1000" ;;
  jets) IN=$S/p3_ref/jets_hold500.root; EX="-n 500 -J" ;;  # jets_1000 entries 500-999 (verified skim)
  cube50|cube50_highPt) IN=$CTX; EX="-n 10000" ;;  # 10-muon guns, vxy up to 50 cm (never trained)
  *) echo "unknown context"; exit 2 ;;
esac
TAG=${ARM}_${CTX}; OUT=$O/nt/$TAG.root
SLOTDIR=$S/displaced_ref/fan/.slots; mkdir -p $SLOTDIR; got=0
while [ $got -eq 0 ]; do
  for i in 1 2 3 4; do exec 9>>"$SLOTDIR/slot$i"; if flock -n 9; then got=1; break; fi; exec 9>&-; done
  [ $got -eq 0 ] && sleep 15
done
while :; do FR=$(free -g | awk '/^Mem:/{print $7}'); [ "${FR:-0}" -ge 100 ] && break; sleep 20; done
export SCRAM_ARCH=el9_amd64_gcc13
pushd $S >/dev/null; source setup.sh >/dev/null 2>&1; eval $(scramv1 runtime -sh 2>/dev/null); source setup.sh >/dev/null 2>&1; popd >/dev/null
export LD_LIBRARY_PATH=$T/bin/$ARM:$LD_LIBRARY_PATH
echo "$(date '+%F %T') start $TAG" >> $O/PROGRESS.txt
$T/bin/$ARM/lst_cpu -i $IN $EX -p 0.8 -s 16 -v 1 -w 1 -o $OUT > $O/logs/$TAG.log 2>&1; rc=$?
$S/efficiency/bin/createPerfNumDenHists -i $OUT -o $O/numden/$TAG.root > $O/logs/numden_$TAG.log 2>&1
python3 $T/summarize.py $OUT $O/sum/$TAG.json > $O/logs/sum_$TAG.log 2>&1
echo "$(date '+%F %T') done $TAG rc=$rc numden=$(ls -s $O/numden/$TAG.root 2>/dev/null | cut -d' ' -f1)" >> $O/PROGRESS.txt
