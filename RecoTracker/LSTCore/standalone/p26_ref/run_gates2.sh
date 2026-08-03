#!/bin/bash
# P2.6a final gate sweep against the FINAL binaries. STRICTLY SEQUENTIAL.
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p26_ref"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

echo "=== 1 timing FINAL, cuda ==="
bash "$REF/run_timing.sh" fin cuda 20 > "$REF/out_timing_fin.txt" 2>&1
echo "=== 2 timing FINAL, cpu ==="
bash "$REF/run_timing.sh" fin cpu 20 >> "$REF/out_timing_fin.txt" 2>&1

echo "=== 3 bit-identity, cpu ==="
bash "$REF/run_bitcheck.sh" cpu 10 fin > "$REF/out_bit2_cpu.txt" 2>&1
echo "=== 4 bit-identity, cuda ==="
bash "$REF/run_bitcheck.sh" cuda 10 fin > "$REF/out_bit2_cuda.txt" 2>&1

echo "=== 5 CPU ls_* defect baseline: BEFORE binary against itself ==="
rm -f "$REF/dfl_A.root" "$REF/dfl_B.root"
for leg in A B; do
  "$REF/bin_before/lst_cpu" -i PU200RelVal -n 10 -s 1 --allobj --use_chain_tracking \
    -o "$REF/dfl_${leg}.root" > "$REF/dfl_${leg}.log" 2>&1
done
python3 "$STANDALONE/p20_bitcheck.py" "$REF/dfl_A.root" "$REF/dfl_B.root" > "$REF/out_defect.txt" 2>&1

echo "=== 5b GPU nondeterminism FLOOR: BEFORE binary twice ==="
rm -f "$REF"/floor_[AB]_tc.bin "$REF"/floor_[AB]_ch.bin
for leg in A B; do
  LST_CHAIN_TC_DUMP="$REF/floor_${leg}_tc.bin" LST_CHAIN_CHAIN_DUMP="$REF/floor_${leg}_ch.bin" \
    "$REF/bin_before/lst_cuda" -i PU200RelVal -n 10 -s 1 --allobj --use_chain_tracking \
    -o "$REF/floor_${leg}.root" > "$REF/floor_${leg}.log" 2>&1
done
{
  echo "################ GPU run-to-run, PRE-CHANGE binary (the floor) ################"
  python3 "$STANDALONE/p25_ref/p25_repro.py" chain "$REF/floor_A_ch.bin" "$REF/floor_B_ch.bin"
  python3 "$STANDALONE/p25_ref/p25_repro.py" tc    "$REF/floor_A_tc.bin" "$REF/floor_B_tc.bin"
} > "$REF/out_floor.txt" 2>&1

echo "=== 6 repro harness on the FINAL build ==="
bash "$REF/run_repro.sh" fin 5 1 > "$REF/out_repro5.txt" 2>&1

echo "=== 7 streams -s1 vs -s4 ==="
bash "$REF/run_streams.sh" cpu  10 > "$REF/out_str_cpu.txt"  2>&1
bash "$REF/run_streams.sh" cuda 10 > "$REF/out_str_cuda.txt" 2>&1

echo "=== 8 scoreboard 300 events, cuda ==="
bash "$REF/run_gate_b.sh" cuda 4 > "$REF/out_sb_cuda.txt" 2>&1
echo "=== 9 scoreboard 300 events, cpu ==="
bash "$REF/run_gate_b.sh" cpu 32 > "$REF/out_sb_cpu.txt" 2>&1

echo GATES2_DONE
