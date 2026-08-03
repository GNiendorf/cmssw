#!/bin/bash
# P2.6a full gate sweep. STRICTLY SEQUENTIAL: the timing legs go first and nothing may overlap
# with anything else on the machine.
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p26_ref"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

echo "=== 1 timing AFTER, cuda ==="
bash "$REF/run_timing.sh" after cuda 20 > "$REF/out_timing_after.txt" 2>&1
echo "=== 2 timing AFTER, cpu ==="
bash "$REF/run_timing.sh" after cpu 20 >> "$REF/out_timing_after.txt" 2>&1

echo "=== 3 bit-identity, cpu ==="
bash "$REF/run_bitcheck.sh" cpu 10 bit > "$REF/out_bit_cpu.txt" 2>&1
echo "=== 4 bit-identity, cuda ==="
bash "$REF/run_bitcheck.sh" cuda 10 bit > "$REF/out_bit_cuda.txt" 2>&1

echo "=== 5 GPU nondeterminism FLOOR: before-binary twice ==="
rm -f "$REF/floor_A_tc.bin" "$REF/floor_B_tc.bin" "$REF/floor_A_ch.bin" "$REF/floor_B_ch.bin"
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

echo "=== 6 repro harness on the AFTER build ==="
bash "$REF/run_repro.sh" p26 5 1 > "$REF/out_repro5.txt" 2>&1

echo "=== 7 streams -s1 vs -s4 ==="
bash "$REF/run_streams.sh" cpu  10 > "$REF/out_str_cpu.txt"  2>&1
bash "$REF/run_streams.sh" cuda 10 > "$REF/out_str_cuda.txt" 2>&1

echo GATES_DONE
