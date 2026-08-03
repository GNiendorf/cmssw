#!/bin/bash
# P2.5 reproducibility legs: two identical invocations of the same backend, plus the
# cross-backend leg. Sidecars are keyed on stable hit rows by p25_repro.py.
#
# usage: run_repro.sh <tag> <nevents> [streams]
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p26_ref"
TAG="${1:-base}"
N="${2:-5}"
S="${3:-1}"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

set -e
for leg in cpuA cpuB gpuA gpuB; do
  rm -f "$REF/${TAG}_${leg}.root" "$REF/${TAG}_${leg}_ch.bin" "$REF/${TAG}_${leg}_tc.bin"
done

run() {   # $1 = exe   $2 = leg name
  LST_CHAIN_CHAIN_DUMP="$REF/${TAG}_$2_ch.bin" LST_CHAIN_TC_DUMP="$REF/${TAG}_$2_tc.bin" \
    "$1" -i PU200RelVal -n "$N" -s "$S" --allobj --use_chain_tracking \
    -o "$REF/${TAG}_$2.root" > "$REF/${TAG}_$2.log" 2>&1
}

echo "[repro] cpu leg A"; run lst_cpu  cpuA
echo "[repro] cpu leg B"; run lst_cpu  cpuB
echo "[repro] gpu leg A"; run lst_cuda gpuA
echo "[repro] gpu leg B"; run lst_cuda gpuB

echo
echo "################ CPU run-to-run ################"
python3 "$STANDALONE/p25_ref/p25_repro.py" chain "$REF/${TAG}_cpuA_ch.bin" "$REF/${TAG}_cpuB_ch.bin" || true
python3 "$STANDALONE/p25_ref/p25_repro.py" tc    "$REF/${TAG}_cpuA_tc.bin" "$REF/${TAG}_cpuB_tc.bin" || true
echo
echo "################ GPU run-to-run ################"
python3 "$STANDALONE/p25_ref/p25_repro.py" chain "$REF/${TAG}_gpuA_ch.bin" "$REF/${TAG}_gpuB_ch.bin" || true
python3 "$STANDALONE/p25_ref/p25_repro.py" tc    "$REF/${TAG}_gpuA_tc.bin" "$REF/${TAG}_gpuB_tc.bin" || true
echo
echo "################ CPU vs GPU ################"
python3 "$STANDALONE/p25_ref/p25_repro.py" chain "$REF/${TAG}_cpuA_ch.bin" "$REF/${TAG}_gpuA_ch.bin" || true
python3 "$STANDALONE/p25_ref/p25_repro.py" tc    "$REF/${TAG}_cpuA_tc.bin" "$REF/${TAG}_gpuA_tc.bin" || true
