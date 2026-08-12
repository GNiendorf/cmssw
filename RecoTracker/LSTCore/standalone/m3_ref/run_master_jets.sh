#!/bin/bash
# M3 (a): LST MASTER (g3, b42d8f97ad5) jet physics reference.
# Pure physics run: -w 1, -J (jet branches), NO timing claim is made from this run.
# usage: run_master_jets.sh <nevents> <streams> <outtag>
N=${1:-1000}
S=${2:-8}
TAG=${3:-master_jets1000}
M3=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/m3_ref
G3=/mnt/data1/gsn27/here/gpu_wt/g3/src/RecoTracker/LSTCore/standalone
IN=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/jet_ref/trackingNtuple_jets_1000.root
cd $G3 || exit 9
source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) 2>/dev/null
source setup.sh > /dev/null 2>&1
./bin/lst_cpu -i "$IN" -n $N -s $S -v 2 -w 1 -J -o $M3/$TAG.root > $M3/$TAG.log 2>&1
echo "rc=$?"
