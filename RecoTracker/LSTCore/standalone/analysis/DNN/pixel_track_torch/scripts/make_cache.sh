#!/bin/bash
# make_cache.sh <sample> <lfn list> <outdir>: one cache job per RAW file (cfg/cache_cfg.py), streamed by xrootd, at most
# NSLOTS at a time through SLOT (a slot wrapper "slot.sh <slotdir> <n> <cmd...>"; default: run directly). Resumable.
# env: PTT_AREA (scram area with LocalVal/PttTruth built), PTT_MENU (full-menu config), X509_USER_PROXY,
#      NSLOTS (3), SLOT, SLOTDIR, PTT_REDIRECTOR (root://cmsxrootd.fnal.gov/), PTT_NEV (-1)
D=$(cd $(dirname $0)/.. && pwd)
if [ "$1" = "--job" ]; then
  shift; S=$1; LFN=$2; OUT=$3; TAG=$(basename $LFN .root | cut -c1-8)
  [ -s $OUT/cache_$TAG.root ] && { echo "SKIP $TAG"; exit 0; }
  source /cvmfs/cms.cern.ch/cmsset_default.sh >/dev/null 2>&1; export SCRAM_ARCH=el9_amd64_gcc14 PYTHONDONTWRITEBYTECODE=1
  cd $PTT_AREA/src && eval $(scramv1 runtime -sh) 2>/dev/null && cd $OUT || exit 1
  export CUDA_VISIBLE_DEVICES="" PTT_DIR=$D PTT_RAW=${PTT_REDIRECTOR:-root://cmsxrootd.fnal.gov/}$LFN PTT_OUT=$OUT/tmp_$TAG.root
  t0=$(date +%s)
  cmsRun $D/cfg/cache_cfg.py > logs/cache_$TAG.log 2>&1; rc=$?
  n=$(grep -m1 'TrigReport Events total' logs/cache_$TAG.log | awk '{print $5}')
  if [ $rc -eq 0 ] && [ -s tmp_$TAG.root ]; then
    mv tmp_$TAG.root cache_$TAG.root; echo "cache_$TAG.root $LFN" >> RAW_MAP.txt
    echo "OK $TAG rc $rc events $n sec $(( $(date +%s) - t0 )) MB $(( $(stat -c %s cache_$TAG.root) / 1000000 ))"
  else
    rm -f tmp_$TAG.root; echo "FAIL $TAG rc $rc"
  fi
  gzip -f logs/cache_$TAG.log; exit $rc
fi
S=${1:?sample}; LIST=$(readlink -f ${2:?lfn list}); OUT=$(readlink -m ${3:?outdir})
mkdir -p $OUT/logs
for lfn in $(grep -v '^#' $LIST | awk 'NF{print $1}'); do
  if [ -n "$SLOT" ]; then
    $SLOT ${SLOTDIR:-$OUT/slots} ${NSLOTS:-3} bash $0 --job $S $lfn $OUT >> $OUT/logs/make_cache.txt 2>&1 &
    sleep 2
  else
    bash $0 --job $S $lfn $OUT >> $OUT/logs/make_cache.txt 2>&1
  fi
done
wait
echo "done $S: OK $(grep -c '^OK\|^SKIP' $OUT/logs/make_cache.txt) FAIL $(grep -c '^FAIL' $OUT/logs/make_cache.txt)"
