#!/bin/bash
# P2.5 second closing measurement, after the attach -RD comparator was left at its P2.4 form
# (class ii: the tie it decides never occurs, and substituting the stable key cost 1.1 ms/evt on
# CUDA). Timing, then the reproducibility legs, then the frozen scoreboard re-verification.
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p25_ref"

echo "=== timing after4 ==="
bash "$REF/run_timing.sh" after4 cpu  20 > "$REF/out_timing_after4.txt"  2>&1
bash "$REF/run_timing.sh" after4 cuda 20 >> "$REF/out_timing_after4.txt" 2>&1
echo "=== repro 5 ==="
bash "$REF/run_repro.sh" fin 5 1 > "$REF/out_repro5d.txt" 2>&1
echo "=== gpu repro 30 ==="
bash "$REF/run_repro30.sh" 30 > "$REF/out_repro30c.txt" 2>&1
echo "=== streams ==="
bash "$REF/run_streams.sh" cuda 10 > "$REF/out_str_cuda2.txt" 2>&1
echo "=== gate a ==="
bash "$REF/run_gate_a.sh" 10 > "$REF/out_gate_a2.txt" 2>&1 || true
echo "=== gate b 300 ==="
bash "$REF/run_gate_b.sh" p25final300 > "$REF/out_gate_b2.txt" 2>&1
echo CLOSE2_DONE
