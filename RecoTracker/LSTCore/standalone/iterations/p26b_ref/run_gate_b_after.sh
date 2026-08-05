#!/bin/bash
# Chain gate (b) behind the gate suite so nothing shares the machine with a timing leg.
REF=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/p26b_ref
until grep -q "ALL_GATES_DONE" "$REF/all_gates.out" 2>/dev/null; do sleep 60; done
echo "gates finished, starting the 300-event scoreboard"
"$REF/run_gate_b.sh" cpu 32 > "$REF/fin_gate_b_cpu.out" 2>&1
echo "GATE_B_DONE"
