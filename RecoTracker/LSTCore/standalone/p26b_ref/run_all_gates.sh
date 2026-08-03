#!/bin/bash
# P2.6b gate suite, strictly sequential (timing legs must not share the machine).
REF=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/p26b_ref
echo "########## GATE (a) bit-identity, both backends ##########"
"$REF/run_bitboth.sh" fin 10 > "$REF/fin_bit.out" 2>&1
grep -E "mismatches |IDENTITY =|bitcheck " "$REF/fin_bit.out"
echo
echo "########## GATE (c) timing, sequential ##########"
"$REF/run_timing_both.sh" fin 20 > "$REF/fin_timing.out" 2>&1
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
