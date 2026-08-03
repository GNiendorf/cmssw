#!/bin/bash
# Third salvage wave: split the three remaining stalled shares once more.
#   c3s11  (-j 16 -I 11) -> -j 64  {11,27,43,59}
#   c0as48 (-j 64 -I 48) -> -j 256 {48,112,176,240}
#   c0bs56 (-j 64 -I 56) -> -j 256 {56,120,184,248}
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
G=$S/rebase_ref/gen
pushd "$S" > /dev/null || exit 1
source setup.sh > /dev/null 2>&1; cmsenv > /dev/null 2>&1; source setup.sh > /dev/null 2>&1
launch() { local NJ=$1 IX=$2 TAG=$3; local OUT="$G/LSTNtuple_instr_1000evt_${TAG}.root"; rm -f "$OUT"
  "$S/bin/lst_cpu" -i PU200RelVal --allobj -n 1000 -s 4 -p 0.8 -v 1 -j "$NJ" -I "$IX" -o "$OUT" > "$G/${TAG}.log" 2>&1 &
  echo "[salvage3] $TAG (-j $NJ -I $IX) pid $!"; }
for IX in 11 27 43 59;      do launch 64  $IX "c3t${IX}"; done
for IX in 48 112 176 240;   do launch 256 $IX "c0at${IX}"; done
for IX in 56 120 184 248;   do launch 256 $IX "c0bt${IX}"; done
wait
echo "[salvage3] done"
