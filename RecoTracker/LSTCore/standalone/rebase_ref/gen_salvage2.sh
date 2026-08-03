#!/bin/bash
# gen_salvage2.sh -- second salvage wave. Every loss so far is one pathological event
# stalling the writer's cartesian-product sim matcher, and a killed job loses its WHOLE
# file (ROOT never writes the TTree). The fix is finer partitions: the smaller the
# sub-chunk, the less a single bad event costs.
#   c3   (-j 4 -I 3)  -> four -j 16 sub-chunks  {3,7,11,15}
#   c0s0 (-j 16 -I 0) -> four -j 64 sub-chunks  {0,16,32,48}
#   c0s8 (-j 16 -I 8) -> four -j 64 sub-chunks  {8,24,40,56}
# Each index set is exactly the parent's, so the union is unchanged.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
G=$S/rebase_ref/gen
mkdir -p "$G"
pushd "$S" > /dev/null || exit 1
source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

launch() {  # launch <njob> <index> <tag>
  local NJ=$1 IX=$2 TAG=$3
  local OUT="$G/LSTNtuple_instr_1000evt_${TAG}.root"
  rm -f "$OUT"
  "$S/bin/lst_cpu" -i PU200RelVal --allobj -n 1000 -s 8 -p 0.8 -v 1 \
      -j "$NJ" -I "$IX" -o "$OUT" > "$G/${TAG}.log" 2>&1 &
  echo "[salvage2] launched $TAG (-j $NJ -I $IX) pid $!"
}

for IX in 3 7 11 15;    do launch 16 $IX "c3s${IX}";  done
for IX in 0 16 32 48;   do launch 64 $IX "c0as${IX}"; done
for IX in 8 24 40 56;   do launch 64 $IX "c0bs${IX}"; done
wait
echo "[salvage2] done"
