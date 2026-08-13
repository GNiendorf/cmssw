#!/bin/bash
# TRIM-NN: LST_CHAIN_CHAIN_DUMP run of the r2 probe binary, one arm per invocation, with a
# provenance stamp written next to the dump (PLAN_NN_LOOP rule).
#   dumprun.sh <TAG> <jets|pu> <NEVT> [ENV=V ...]
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
A=/mnt/data1/gsn27/here/gpu_wt/tn1/src/RecoTracker/LSTCore/standalone
R=$S/trimnn_ref/runs; D=$S/trimnn_ref/dump
JETS=$S/jet_ref/trackingNtuple_jets_1000.root
PU=/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/event_1000.root
TAG=$1; WHAT=$2; NEVT=$3; shift 3
mkdir -p $R $D
cd $A || exit 9
source setup.sh > /dev/null 2>&1; eval $(scramv1 runtime -sh) 2>/dev/null; source setup.sh > /dev/null 2>&1
export LD_LIBRARY_PATH=$A/LST:$LD_LIBRARY_PATH
BIN=$A/bin/lst_cpu
RES=$(ldd $BIN 2>/dev/null | awk '$1 == "liblst_cpu.so" {print $3}')
case "$RES" in $A/LST/*) : ;; *) echo "ABORT: lib -> '${RES:-NOTHING}'"; exit 8 ;; esac
N=${TAG}_${WHAT}
exec 9> $D/.$N.lock
flock -n 9 || { echo "ANOTHER $N DUMP IS RUNNING -- refusing"; exit 9; }
{
  echo "tag        $N"
  echo "date       $(date -Is)"
  echo "tree       $A"
  echo "git        $(git -C $A/.. rev-parse HEAD)"
  echo "dirty_LST  $(git -C $A/.. status --porcelain ../src ../interface | wc -l)"
  echo "binary_md5 $(md5sum $BIN | cut -d' ' -f1)"
  for h in ChainNetworkWeights EdgeNetworkWeights AttachNetworkWeights T3NeuralNetworkWeights; do
    echo "hdr_$h $(md5sum $A/../src/alpaka/$h.h | cut -d' ' -f1)"
  done
  echo "chaincfg   $(md5sum $A/../interface/ChainConfig.h | cut -d' ' -f1)"
  echo "sample     $WHAT nevt $NEVT"
  echo "env        $*"
} > $D/$N.prov
rm -f $D/$N.bin
case $WHAT in
  jets) env LST_CHAIN_VARIANT_DUMP=$D/$N.bin "$@" $BIN -i $JETS -n $NEVT -p 0.8 -s 1 -w 0 -J -o $R/$N.root > $R/$N.log 2>&1 ;;
  pu)   env LST_CHAIN_VARIANT_DUMP=$D/$N.bin "$@" $BIN -i $PU   -n $NEVT -p 0.8 -s 1 -w 0    -o $R/$N.root > $R/$N.log 2>&1 ;;
esac
echo "RUN_EXIT=$?" >> $R/$N.log
{ echo "overflow   $(grep -c 'CHAIN OVERFLOW' $R/$N.log)"
  echo "trimline   $(grep -ah 'terminal trim resolved' $R/$N.log | head -1)"
  echo "bytes      $(stat -c %s $D/$N.bin)"
  echo "dump_md5   $(md5sum $D/$N.bin | cut -d' ' -f1)"; } >> $D/$N.prov
rm -f $R/$N.root
echo "TRIMNN-DUMP DONE $N"
