#!/bin/bash
# T4 DNN training samples from the T4 training build (T5 candidate, T4 DNN creation cut off, t4x_* features, -d). Outputs only under samples_t4/.
T=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/displaced_ref/t5dnn
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
B=$T/bin/t4train
export SCRAM_ARCH=el9_amd64_gcc13
pushd $S >/dev/null; source setup.sh >/dev/null 2>&1; eval $(scramv1 runtime -sh 2>/dev/null); source setup.sh >/dev/null 2>&1; popd >/dev/null
unset LST_MODULE_MAP
export LD_LIBRARY_PATH=$B:$LD_LIBRARY_PATH
PROV="bin_md5=$(md5sum $B/lst_cpu | cut -c1-12) lib_md5=$(md5sum $B/liblst_cpu.so | cut -c1-12) git=$(git -C $T/wt_t4train rev-parse --short=12 HEAD) diff_md5=$(md5sum $T/samples_t4/train_build.diff | cut -c1-12)"
run_chunk() { # smp in n nsplit k extra
  local smp=$1 in=$2 n=$3 ns=$4 k=$5 extra=$6 out=$T/samples_t4/$1/chunk_$5.root log=$T/samples_t4/logs/$1_$5.log
  [ -s $out.done ] && { echo "SKIP $smp $k"; return 0; }
  for try in 1 2 3; do
    rm -f $out
    $B/lst_cpu -i $in -n $n -p 0.8 -s 16 -v 1 -w 1 --t4 --t4dnn $extra --nsplit_jobs $ns --job_index $k -o $out > $log 2>&1 &
    local pid=$! last=$(date +%s)
    while kill -0 $pid 2>/dev/null; do
      sleep 30
      local sz=$(( $(stat -c %s $log 2>/dev/null || echo 0) + $(stat -c %s $out 2>/dev/null || echo 0) ))
      if [ "$sz" != "${prev:-}" ]; then prev=$sz; last=$(date +%s); fi
      if [ $(( $(date +%s) - last )) -gt 900 ]; then echo "STALL $smp $k try $try -> kill"; kill -9 $pid; break; fi
    done
    wait $pid; rc=$?
    if [ $rc -eq 0 ] && [ -s $out ]; then echo "$PROV cmd=\"-i $in -n $n --nsplit_jobs $ns --job_index $k $extra\"" > $out.done; echo "OK $smp $k"; return 0; fi
    echo "FAIL $smp $k try $try rc=$rc"
  done
  return 1
}
export -f run_chunk; export T B PROV
PU=/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/event_2000.root
JET=$S/jet_ref/trackingNtuple_jets_1000.root
GUN=/data2/segmentlinking/CMSSW_12_2_0_pre2/trackingNtuple_10mu_10k_pt_0p5_50_5cm_cube.root
{ for k in $(seq 0 9); do echo "pu $PU 1000 10 $k"; done
  for k in $(seq 0 4); do echo "jet $JET 500 5 $k -J"; done
  for k in $(seq 0 3); do echo "gun $GUN 10000 4 $k"; done; } | xargs -P 4 -L 1 bash -c 'run_chunk "$0" "$1" "$2" "$3" "$4" "$5"'
echo "GEN DONE"
