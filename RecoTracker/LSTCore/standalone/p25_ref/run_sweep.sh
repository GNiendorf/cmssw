#!/bin/bash
# P2.5 full sweep with the fixed binaries, strictly sequential.
#   1  timing AFTER (cpu then cuda)   -- FIRST, so nothing else is on the machine
#   2  reproducibility, 5 events, all four legs
#   3  reproducibility, GPU only, 30 events
#   4  node/edge audit: weld-tie uniqueness census + CPU-vs-GPU attribution, 10 events
#   5  stream-count race check, cpu and cuda, 10 events
#   6  gate (a): 10-event TC parity against the frozen prototype
#   7  gate (b): the 300-event physics scoreboard
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p25_ref"

echo "=== 1 timing AFTER ==="
bash "$REF/run_timing.sh" after cpu  20 > "$REF/out_timing_after.txt"  2>&1
bash "$REF/run_timing.sh" after cuda 20 >> "$REF/out_timing_after.txt" 2>&1

echo "=== 2 repro 5 ==="
bash "$REF/run_repro.sh" fix2 5 1 > "$REF/out_repro5.txt" 2>&1

echo "=== 3 repro GPU 30 ==="
bash "$REF/run_repro30.sh" > "$REF/out_repro30.txt" 2>&1

echo "=== 4 audit 10 ==="
bash "$REF/run_audit.sh" 10 > "$REF/out_audit10.txt" 2>&1

echo "=== 5 streams ==="
bash "$REF/run_streams.sh" cpu  10 > "$REF/out_str_cpu.txt"  2>&1
bash "$REF/run_streams.sh" cuda 10 > "$REF/out_str_cuda.txt" 2>&1

echo "=== 6 gate a ==="
bash "$REF/run_gate_a.sh" 10 > "$REF/out_gate_a.txt" 2>&1

echo "=== 7 gate b (300 events) ==="
bash "$REF/run_gate_b.sh" p25prod300 > "$REF/out_gate_b.txt" 2>&1

echo SWEEP_DONE
