#!/bin/bash
# P2.5 full correctness sweep with the fixed binaries. Sequential on purpose: nothing here may
# overlap with anything else on the machine, the timing legs least of all.
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p25_ref"
set -x
bash "$REF/run_repro.sh"  fix2 5 1   > "$REF/out_repro5.txt"    2>&1
bash "$REF/run_audit.sh"  10         > "$REF/out_audit10.txt"   2>&1
bash "$REF/run_streams.sh" cpu  10   > "$REF/out_str_cpu.txt"   2>&1
bash "$REF/run_streams.sh" cuda 10   > "$REF/out_str_cuda.txt"  2>&1
echo ALLDONE
