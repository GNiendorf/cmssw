#!/bin/bash
# FALLBACK, offline only (hadd + python on already-written files, broker-exempt): if the capped arm
# is NOT bit-identical on jets, the honest number is the physics delta, not the branch diff.
# Concatenates the per-event ntuples of m2_ref/j100 into one file per arm and runs M3's
# m3_ref/jetphys.py on both, so eff / fake / dup and the dR bands come out of the SAME tool M3's
# baseline used.  Events 5 and 85 never enter either arm (no cap-off reference exists for them).
# usage: jetphys_arms.sh <C>
H=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
cd $H || exit 9
source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
C=${1:-32}
O=$H/m2_ref/j100
for arm in 0 $C; do
  rm -f $O/arm_C$arm.root
  hadd -f $O/arm_C$arm.root $O/C${arm}_evt*.root > $O/hadd_C$arm.log 2>&1
  echo "arm C=$arm : $(python3 -c "import uproot,sys;print(uproot.open('$O/arm_C$arm.root')['tree'].num_entries,'entries')" 2>/dev/null)"
done
python3 $H/m3_ref/jetphys.py $O/arm_C0.root $O/arm_C$C.root --json $H/m2_ref/jetphys_C$C.json
