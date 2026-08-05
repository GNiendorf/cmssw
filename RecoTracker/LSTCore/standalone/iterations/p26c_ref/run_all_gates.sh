#!/bin/bash
# P2.6c gate suite, strictly sequential (timing legs must never share the machine).
REF=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/p26c_ref

echo "########## GATE (a) bit-identity, both backends ##########"
"$REF/run_bitboth.sh" fin 10 > "$REF/fin_bit.out" 2>&1
grep -E "mismatches |IDENTITY =|bitcheck |ok: |CONTROL FAIL" "$REF/fin_bit.out"

echo
echo "########## GATE (c) timing, sequential, before then after ##########"
"$REF/run_timing.sh" fin cpu  before 20 > "$REF/fin_t_cpu_before.out"  2>&1
"$REF/run_timing.sh" fin cpu  after  20 > "$REF/fin_t_cpu_after.out"   2>&1
"$REF/run_timing.sh" fin cuda before 20 > "$REF/fin_t_cuda_before.out" 2>&1
"$REF/run_timing.sh" fin cuda after  20 > "$REF/fin_t_cuda_after.out"  2>&1
echo "--- CPU  before vs after ---"
python3 "$REF/p26c_timing.py" "$REF/t_fin_cpu_before.log"  "$REF/t_fin_cpu_after.log"
echo "--- CUDA before vs after ---"
python3 "$REF/p26c_timing.py" "$REF/t_fin_cuda_before.log" "$REF/t_fin_cuda_after.log"

echo
echo "########## repro run-to-run + cross-backend ##########"
"$REF/run_repro.sh" fin 5 1 > "$REF/fin_repro.out" 2>&1
grep -E "IDENTITY =|####" "$REF/fin_repro.out"

echo
echo "########## stream invariance -s1 vs -s4 ##########"
"$REF/run_streams.sh" cpu 10  > "$REF/fin_str_cpu.out" 2>&1
"$REF/run_streams.sh" cuda 10 > "$REF/fin_str_cuda.out" 2>&1
grep -E "IDENTITY =|####" "$REF/fin_str_cpu.out" "$REF/fin_str_cuda.out"
echo "ALL_GATES_DONE"
