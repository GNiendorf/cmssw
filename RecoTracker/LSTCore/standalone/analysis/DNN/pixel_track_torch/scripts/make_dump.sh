#!/bin/bash
# make_dump.sh <sample> <cache dir> <outdir> [files per job]: on-policy dumps (cfg/dump_cfg.py), one job per group of
# cache files (default one; a job is named after its first file), at most NSLOTS at a time through SLOT (default: run directly). Resumable.
# env: PTT_AREA (area with LocalVal/PttDump and the branch built), PTT_MENU (full-menu CPU config with the policy),
#      PTT_DEP1 (optional), PTT_FILES_LIMIT (first N cache files only), NSLOTS (3), SLOT, SLOTDIR, PTT_THREADS (8)
D=$(cd $(dirname $0)/.. && pwd)
if [ "$1" = "--job" ]; then
  shift; TAG=$1; FILES=$2; OUT=$3
  [ -s $OUT/dump_$TAG.root ] && { echo "SKIP $TAG"; exit 0; }
  source /cvmfs/cms.cern.ch/cmsset_default.sh >/dev/null 2>&1; export SCRAM_ARCH=el9_amd64_gcc14 PYTHONDONTWRITEBYTECODE=1
  cd $PTT_AREA/src && eval $(scramv1 runtime -sh) 2>/dev/null && cd $OUT || exit 1
  export CUDA_VISIBLE_DEVICES="" PTT_DIR=$D PTT_CACHE=$FILES PTT_OUT=$OUT/tmp_$TAG.root
  t0=$(date +%s)
  cmsRun $D/cfg/dump_cfg.py > logs/dump_$TAG.log 2>&1; rc=$?
  n=$(grep -m1 'TrigReport Events total' logs/dump_$TAG.log | awk '{print $5}')
  if [ $rc -eq 0 ] && [ -s tmp_$TAG.root ]; then
    mv tmp_$TAG.root dump_$TAG.root; echo "OK $TAG rc $rc events $n sec $(( $(date +%s) - t0 ))"
  else
    rm -f tmp_$TAG.root; echo "FAIL $TAG rc $rc"
  fi
  gzip -f logs/dump_$TAG.log; exit $rc
fi
S=${1:?sample}; C=$(readlink -f ${2:?cache dir}); OUT=$(readlink -m ${3:?outdir}); K=${4:-1}
mkdir -p $OUT/logs
files=($(ls $C/cache_*.root | head -n ${PTT_FILES_LIMIT:-100000}))
for ((i = 0; i < ${#files[@]}; i += K)); do
  grp=$(IFS=,; echo "${files[*]:i:K}"); tag=${S}_$(basename ${files[i]} .root | sed 's/^cache_//')
  if [ -n "$SLOT" ]; then
    $SLOT ${SLOTDIR:-$OUT/slots} ${NSLOTS:-3} bash $0 --job $tag $grp $OUT >> $OUT/logs/make_dump.txt 2>&1 &
    sleep 2
  else
    bash $0 --job $tag $grp $OUT >> $OUT/logs/make_dump.txt 2>&1
  fi
done
wait
echo "done $S: OK $(grep -c '^OK\|^SKIP' $OUT/logs/make_dump.txt) FAIL $(grep -c '^FAIL' $OUT/logs/make_dump.txt)"
