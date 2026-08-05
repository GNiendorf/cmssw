#!/bin/bash
# P2.5 closing measurement after the SoA layout fix.
#   1  timing after3, and a SECOND identical leg (after3b) so the run-to-run spread of the timing
#      harness itself is on the record next to the before/after delta
#   2  repro 5, all four legs, as the final correctness check
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p25_ref"

echo "=== timing after3 ==="
bash "$REF/run_timing.sh" after3 cpu  20 > "$REF/out_timing_after3.txt"  2>&1
bash "$REF/run_timing.sh" after3 cuda 20 >> "$REF/out_timing_after3.txt" 2>&1
echo "=== timing after3b (same binary, noise leg) ==="
bash "$REF/run_timing.sh" after3b cpu  20 >> "$REF/out_timing_after3.txt" 2>&1
bash "$REF/run_timing.sh" after3b cuda 20 >> "$REF/out_timing_after3.txt" 2>&1
echo "=== repro 5 ==="
bash "$REF/run_repro.sh" fix4 5 1 > "$REF/out_repro5c.txt" 2>&1
echo "=== gpu repro 30 ==="
bash "$REF/run_repro30.sh" 30 > "$REF/out_repro30b.txt" 2>&1
echo CLOSE_DONE
