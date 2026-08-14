#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
cd $S || exit 9
source setup.sh > /dev/null 2>&1; eval $(scramv1 runtime -sh) 2>/dev/null; source setup.sh > /dev/null 2>&1
export LD_LIBRARY_PATH=$S/LST:$LD_LIBRARY_PATH
echo "MAIN $(md5sum $S/bin/lst_cpu|cut -d' ' -f1) NO ENV (D128 is the default)"
rm -f $S/w5_ref/MAINCHK_jet.root
$S/bin/lst_cpu -i $S/jet_ref/trackingNtuple_jets_1000.root -n 500 -p 0.8 -s 16 -w 1 -J \
   -o $S/w5_ref/MAINCHK_jet.root > $S/w5_ref/MAINCHK_jet.log 2>&1
echo "rc=$?"
