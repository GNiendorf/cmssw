#!/bin/bash
# Coordinator SEALED gate for UN4. One binary (N4's snapshot), arms differ by env only.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
A=/mnt/data1/gsn27/here/gpu_wt/g5/src/RecoTracker/LSTCore/standalone
R=$S/un4_ref/runs
JETS=$S/jet_ref/trackingNtuple_jets_1000.root
PU2=/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/event_2000.root
TAG=$1; WHAT=$2; shift 2
mkdir -p $R
cd $A || exit 9
source setup.sh > /dev/null 2>&1; eval $(scramv1 runtime -sh) 2>/dev/null; source setup.sh > /dev/null 2>&1
BIN=$S/n4_ref/snap/bin/lst_cpu
export LD_LIBRARY_PATH=$S/n4_ref/snap/LST:$LD_LIBRARY_PATH
RES=$(ldd $BIN 2>/dev/null | awk '$1 == "liblst_cpu.so" {print $3}')
case "$RES" in $S/n4_ref/snap/LST/*) : ;; *) echo "ABORT: lib resolves to '${RES:-NOTHING}'"; exit 8 ;; esac
echo "BIN $(md5sum $BIN|cut -d' ' -f1) lib $(md5sum $RES|cut -d' ' -f1) ENV: $*"
rm -f $R/${TAG}_${WHAT}.root
case $WHAT in
  jets)   env "$@" $BIN -i $JETS -n 1000 -p 0.8 -s 16 -w 1 -J -o $R/${TAG}_jets.root  > $R/${TAG}_jets.log 2>&1 ;;
  pu2)    env "$@" $BIN -i $PU2  -n 1000 -p 0.8 -s 8  -w 1    -o $R/${TAG}_pu2.root   > $R/${TAG}_pu2.log 2>&1 ;;
  cubehi) env "$@" $BIN -i cube50_highPt -n -1 -p 0.8 -s 4 -w 1 -o $R/${TAG}_cubehi.root > $R/${TAG}_cubehi.log 2>&1 ;;
esac
echo "rc=$?"; grep -ah "chainenv" $R/${TAG}_${WHAT}.log | head -2
echo "UN4-GATE DONE $TAG $WHAT"
