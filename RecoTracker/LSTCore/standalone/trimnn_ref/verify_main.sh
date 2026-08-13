#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R=$S/trimnn_ref/verify
JETS=$S/jet_ref/trackingNtuple_jets_1000.root
PU2=/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/event_2000.root
mkdir -p $R; cd $S || exit 9
source setup.sh > /dev/null 2>&1; eval $(scramv1 runtime -sh) 2>/dev/null; source setup.sh > /dev/null 2>&1
export LD_LIBRARY_PATH=$S/LST:$LD_LIBRARY_PATH
BIN=$S/bin/lst_cpu
echo "MAIN-TREE BIN $(md5sum $BIN|cut -d' ' -f1)  NO ENV SET (shipped default)"
rm -f $R/MAIN_jets.root $R/MAIN_pu2.root
$BIN -i $JETS -n 1000 -p 0.8 -s 16 -w 1 -J -o $R/MAIN_jets.root > $R/MAIN_jets.log 2>&1; echo "jets rc=$?"
$BIN -i $PU2  -n 1000 -p 0.8 -s 8  -w 1    -o $R/MAIN_pu2.root  > $R/MAIN_pu2.log 2>&1; echo "pu2 rc=$?"
grep -ah "trim resolved" $R/MAIN_jets.log | head -1; echo "(no line above = defaults, nothing overridden -- correct)"
