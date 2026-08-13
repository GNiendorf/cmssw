#!/bin/bash
# run.sh <TAG> <jets|pu|cube10|cube50> [ENV=V ...]   -- physics run of the tn1 binary, -w 1.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
A=/mnt/data1/gsn27/here/gpu_wt/tn1/src/RecoTracker/LSTCore/standalone
R=$S/trimnn_ref/runs
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
  jets)   env "$@" $BIN -i $JETS -n 500  -p 0.8 -s 16 -w 1 -J -o $R/${TAG}_jets.root   > $R/${TAG}_jets.log 2>&1 ;;
  pu)     env "$@" $BIN -i $PU   -n 1000 -p 0.8 -s 8  -w 1    -o $R/${TAG}_pu.root     > $R/${TAG}_pu.log 2>&1 ;;
  cube50) env "$@" $BIN -i cube50 -n 5000 -p 0.8 -s 4 -w 1    -o $R/${TAG}_cube50.root > $R/${TAG}_cube50.log 2>&1 ;;
  cubehi) for st in 4 2 1 8 16; do
            env "$@" $BIN -i cube50_highPt -n 5000 -p 0.8 -s $st -w 1 -o $R/${TAG}_cubehi.root > $R/${TAG}_cubehi.log 2>&1
            echo "RUN_EXIT=$? streams=$st" >> $R/${TAG}_cubehi.log
            grep -q "RUN_EXIT=139" $R/${TAG}_cubehi.log || { echo "cubehi ok at -s $st"; break; }
            echo "cubehi SEGFAULT at -s $st, dropping"
          done ;;
esac
echo "rc=$?"; grep -ah "terminal trim resolved" $R/${TAG}_${WHAT}.log | head -1
echo "TRIMNN-RUN DONE $TAG $WHAT"
