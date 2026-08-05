#!/bin/bash
# P2.5 closing sweep after the two performance fixes (weld early-out, attach key hoist).
#   1  timing AFTER2 -- first, alone on the machine
#   2  repro 5, all four legs: the fixes must not have changed a single object
#   3  hit-level reproducibility floor, flag-OFF vs flag-ON, using the relocated TC sidecar
#   4  chain-level diff of the tie-break swap (baseline sidecar vs P2.5 sidecar, same 10 events)
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p25_ref"

echo "=== 1 timing AFTER2 ==="
bash "$REF/run_timing.sh" after2 cpu  20 > "$REF/out_timing_after2.txt"  2>&1
bash "$REF/run_timing.sh" after2 cuda 20 >> "$REF/out_timing_after2.txt" 2>&1

echo "=== 2 repro 5 ==="
bash "$REF/run_repro.sh" fix3 5 1 > "$REF/out_repro5b.txt" 2>&1

echo "=== 3 hit-level floor ==="
bash "$REF/run_floor_hits.sh" 10 > "$REF/out_floor_hits.txt" 2>&1

echo "=== 4 tie-break chain diff ==="
cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
rm -f "$REF/p25_ch10.bin"
LST_CHAIN_CHAIN_DUMP="$REF/p25_ch10.bin" lst_cpu -i PU200RelVal -n 10 -s 1 -w 0 --use_chain_tracking \
  -o "$REF/p25_ch10.root" > "$REF/p25_ch10.log" 2>&1
python3 "$REF/p25_tiediff.py" "$REF/base_ch10.bin" "$REF/p25_ch10.bin" > "$REF/out_tiediff.txt" 2>&1

echo FINAL_DONE
