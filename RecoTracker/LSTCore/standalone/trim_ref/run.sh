#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
A=/mnt/data1/gsn27/here/gpu_wt/r2/src/RecoTracker/LSTCore/standalone
R=$S/trim_ref/runs
JETS=$S/jet_ref/trackingNtuple_jets_1000.root
PU=/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/event_1000.root
TAG=$1; WHAT=$2; shift 2
mkdir -p $R
cd $A || exit 9
source setup.sh > /dev/null 2>&1; eval $(scramv1 runtime -sh) 2>/dev/null; source setup.sh > /dev/null 2>&1
export LD_LIBRARY_PATH=$A/LST:$LD_LIBRARY_PATH
BIN=$A/bin/lst_cpu
RES=$(ldd $BIN 2>/dev/null | awk '$1 == "liblst_cpu.so" {print $3}')
case "$RES" in $A/LST/*) : ;; *) echo "ABORT: lib -> '${RES:-NOTHING}'"; exit 8 ;; esac
echo "BIN $(md5sum $BIN|cut -d' ' -f1) ENV: $*"
rm -f $R/${TAG}_${WHAT}.root
case $WHAT in
  jets) env "$@" $BIN -i $JETS -n 500 -p 0.8 -s 16 -w 1 -J -o $R/${TAG}_jets.root > $R/${TAG}_jets.log 2>&1 ;;
  pu)   env "$@" $BIN -i $PU   -n 1000 -p 0.8 -s 8 -w 1   -o $R/${TAG}_pu.root   > $R/${TAG}_pu.log 2>&1 ;;
esac
echo "rc=$?"; grep -ah "terminal trim resolved" $R/${TAG}_${WHAT}.log | head -1
echo "TRIM-RUN DONE $TAG $WHAT"
