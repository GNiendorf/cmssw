#!/bin/bash
# run_chain.sh: one command for the on-policy otpull pair-network chain dump -> rows -> train -> export -> working points.
#   OTP_DUMP_AREA=<scram area with dump.patch built> OTP_ARM=<arm cfg> OTP_WORK=<dir> OTP_TAG=<tag> \
#   OTP_CACHE_TTBAR='<glob>' OTP_CACHE_QCD='<glob>' bash run_chain.sh [dump|rows|train|export|wp|all]
# Optional: OTP_SLOT="<slot wrapper> <slotdir> <n>" (prefix of every cmsRun; default: 3 jobs at a time), OTP_THREADS (16),
#   OTP_EPOCHS (120), OTP_Q (0.5,0.6,0.7,0.8), OTP_SHARE (0.25), OTP_FALSE_KEEP (0.25, train split only),
#   OTP_GPU (0), OTP_HEADER (default: this branch's src/alpaka/OTPullNeuralNetworkWeights.h).
# Every dump job writes <work>/dump/<sample>/<cache tag>/{diag.root, prov.txt (cache, RAW parent, arm, area, git SHA)}.
# Resumable: finished dumps (diag.root + prov.txt) are skipped.
set -u
HERE=$(cd $(dirname $0) && pwd)
STEP=${1:-all}
W=${OTP_WORK:?}; TAG=${OTP_TAG:?}
mkdir -p $W/dump $W/rows $W/models $W/wp $W/logs
env_area() { source /cvmfs/cms.cern.ch/cmsset_default.sh >/dev/null 2>&1; export SCRAM_ARCH=el9_amd64_gcc14
             cd $1/src && eval $(scramv1 runtime -sh) && cd - >/dev/null; }

dump_one() {  # <sample> <cache file>
  local s=$1 c=$2 tag d
  tag=$(basename $c .root); d=$W/dump/$s/$tag; mkdir -p $d
  [ -s $d/diag.root ] && [ -s $d/prov.txt ] && return 0
  [ -e $d/started ] && return 0
  touch $d/started
  ( env_area $OTP_DUMP_AREA; cd $d
    export OTP_CACHE=$c OTP_OUT=$d/diag.root OTP_THREADS=${OTP_THREADS:-16} CUDA_VISIBLE_DEVICES=""
    ${OTP_SLOT:-} cmsRun $HERE/cfg/dump_cfg.py > $d/log.txt 2>&1; rc=$?
    raw=$(awk -v f=$(basename $c) '$1==f{print $2}' $(dirname $c)/RAW_MAP.txt 2>/dev/null)
    if [ $rc -eq 0 ] && [ -s $d/diag.root ]; then
      echo "cache $c raw ${raw:-?} arm $OTP_ARM area $OTP_DUMP_AREA sha $(git -C $HERE rev-parse --short=11 HEAD) events $(grep -m1 'TrigReport Events total' $d/log.txt | awk '{print $5}') date $(date '+%F %-I:%M %p')" > $d/prov.txt
      echo "OK $s $tag"
    else rm -f $d/diag.root $d/started; echo "FAIL $s $tag rc $rc"; fi
    gzip -f $d/log.txt )
}

if [ $STEP = dump ] || [ $STEP = all ]; then
  export OTP_ARM
  for s in ttbar qcd; do
    v=OTP_CACHE_${s^^}
    for c in ${!v}; do
      dump_one $s $(readlink -f $c) >> $W/logs/dump.txt 2>&1 &
      sleep 2
      [ -z "${OTP_SLOT:-}" ] && while [ $(jobs -rp | wc -l) -ge 3 ]; do sleep 5; done
    done
  done
  wait
  echo "dump: OK $(grep -c '^OK' $W/logs/dump.txt) FAIL $(grep -c '^FAIL' $W/logs/dump.txt)"
fi
PY="python3"
if [ $STEP = rows ] || [ $STEP = all ]; then
  ( env_area $OTP_DUMP_AREA
    for s in ttbar qcd; do
      for d in $W/dump/$s/*/; do
        t=$(basename $d); [ -s $W/rows/${s}_$t.npz ] || $PY $HERE/make_rows.py $W/rows/${s}_$t.npz $d/diag.root
      done
    done ) > $W/logs/rows.txt 2>&1
  tail -2 $W/logs/rows.txt
fi
if [ $STEP = train ] || [ $STEP = all ]; then
  ( env_area $OTP_DUMP_AREA; export CUDA_VISIBLE_DEVICES=${OTP_GPU:-0}
    $PY $HERE/train_otpull.py --lab ttbar="$W/rows/ttbar_*.npz" --lab qcd="$W/rows/qcd_*.npz" --ref ttbar \
        --share qcd=${OTP_SHARE:-0.25} --false-keep ${OTP_FALSE_KEEP:-0.25} --epochs ${OTP_EPOCHS:-120} \
        --tag $TAG --out $W/models ) > $W/logs/train_$TAG.txt 2>&1
  grep -v Warning $W/logs/train_$TAG.txt | tail -3
fi
if [ $STEP = export ] || [ $STEP = all ]; then
  ( env_area $OTP_DUMP_AREA
    $PY $HERE/export_otpull.py $W/models/otpull_$TAG.json ${OTP_HEADER:+--out $OTP_HEADER} ) 2>&1 | tail -1
fi
if [ $STEP = wp ] || [ $STEP = all ]; then
  ( env_area $OTP_DUMP_AREA
    $PY $HERE/wp_otpull.py $W/models/otpull_$TAG.json --lab ttbar="$W/rows/ttbar_*.npz" --lab qcd="$W/rows/qcd_*.npz" \
        --q ${OTP_Q:-0.5,0.6,0.7,0.8} --out $W/wp/otpull_$TAG ) > $W/logs/wp_$TAG.txt 2>&1
  grep "objects" $W/logs/wp_$TAG.txt
fi
