#!/bin/bash
# gen_c0_salvage.sh -- chunk 0 stalled on ONE pathological event inside the writer's
# sim matcher (matchedSimTrkIdxsAndFracs builds the full cartesian product of the per-hit
# sim candidate lists, code/core/trkCore.cc:461-473; exponential on dense events).
# `-j 16 -I {0,4,8,12}` selects read-indices n with n % 16 in {0,4,8,12}, i.e. exactly the
# n % 4 == 0 set that `-j 4 -I 0` covers -- so the four sub-chunks reproduce chunk 0
# EXACTLY, and only the one containing the bad event is lost.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
G=$S/rebase_ref/gen
mkdir -p "$G"
pushd "$S" > /dev/null || exit 1
source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

PIDS=()
for I in 0 4 8 12; do
  OUT="$G/LSTNtuple_instr_1000evt_c0s${I}.root"
  rm -f "$OUT"
  "$S/bin/lst_cpu" -i PU200RelVal --allobj -n 1000 -s 16 -p 0.8 -v 1 \
      -j 16 -I "$I" -o "$OUT" > "$G/c0s${I}.log" 2>&1 &
  P=$!
  PIDS+=("$P")
  echo "[salvage] launched sub-chunk $I pid $P -> $OUT"
done
wait
echo "[salvage] done"
