#!/bin/bash
# usage: build_area.sh <name> <LST ref in rebase51866> [mkfit patch]   -> $V/<name> (CMSSW_20_1_0_pre1)
N=$1; REF=$2; MP=$3; V=/mnt/data1/gsn27/here/hltqcd/val5k; R=/mnt/data1/gsn27/here/rebase51866
source /cvmfs/cms.cern.ch/cmsset_default.sh; export SCRAM_ARCH=el9_amd64_gcc13
cd $V && scram project -n $N CMSSW CMSSW_20_1_0_pre1 || exit 1
cd $V/$N/src && eval $(scramv1 runtime -sh)
git cms-init --upstream-only > $V/$N.init.log 2>&1 || { echo "INIT FAILED $N"; exit 1; }
P="RecoTracker/LST RecoTracker/LSTCore"; [ -n "$MP" ] && P="$P RecoTracker/MkFitCMS"
git cms-addpkg $P >> $V/$N.init.log 2>&1 || exit 1
rm -rf RecoTracker/LST RecoTracker/LSTCore
if [ -d "$REF" ]; then cp -a $REF/RecoTracker/LST $REF/RecoTracker/LSTCore RecoTracker/; else git -C $R archive $REF RecoTracker/LST RecoTracker/LSTCore | tar x -C .; fi
[ -n "$MP" ] && { patch -p1 < $MP > $V/$N.patch.log 2>&1 || { echo "PATCH FAILED $N"; exit 1; }; }
git cms-checkdeps -D >> $V/$N.init.log 2>&1
echo "$N: LST from $REF ${MP:++ mkfit patch}; building"; scram b -j 32 > $V/$N.build.log 2>&1
echo "$N build rc=$? errors=$(grep -c 'error:' $V/$N.build.log)"
