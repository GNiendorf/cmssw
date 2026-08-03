#!/bin/bash
# P2.6b gate (a): 10-event --allobj bit-identity of the CURRENT build against the P2.5 reference
# binaries saved in p26_ref/bin_before, on one backend.
#
# usage: run_bitcheck.sh <cpu|cuda> [nevents] [tag]
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p26b_ref"
BK="${1:-cpu}"
N="${2:-10}"
TAG="${3:-bit}"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

set -e
rm -f "$REF/${TAG}_${BK}_before.root" "$REF/${TAG}_${BK}_after.root"
rm -f "$REF/${TAG}_${BK}_before_tc.bin" "$REF/${TAG}_${BK}_after_tc.bin"
rm -f "$REF/${TAG}_${BK}_before_ch.bin" "$REF/${TAG}_${BK}_after_ch.bin"

echo "[bitcheck $BK] leg BEFORE (p26_ref/bin_before/lst_$BK)"
LST_CHAIN_TC_DUMP="$REF/${TAG}_${BK}_before_tc.bin" LST_CHAIN_CHAIN_DUMP="$REF/${TAG}_${BK}_before_ch.bin" \
  "$REF/bin_before/lst_$BK" -i PU200RelVal -n "$N" -s 1 --allobj --use_chain_tracking \
  -o "$REF/${TAG}_${BK}_before.root" > "$REF/${TAG}_${BK}_before.log" 2>&1

echo "[bitcheck $BK] leg AFTER (bin/lst_$BK)"
LST_CHAIN_TC_DUMP="$REF/${TAG}_${BK}_after_tc.bin" LST_CHAIN_CHAIN_DUMP="$REF/${TAG}_${BK}_after_ch.bin" \
  lst_$BK -i PU200RelVal -n "$N" -s 1 --allobj --use_chain_tracking \
  -o "$REF/${TAG}_${BK}_after.root" > "$REF/${TAG}_${BK}_after.log" 2>&1

echo "[bitcheck $BK] ntuple branch-by-branch"
python3 "$STANDALONE/p20_bitcheck.py" "$REF/${TAG}_${BK}_before.root" "$REF/${TAG}_${BK}_after.root" || true
echo "[bitcheck $BK] TC sidecar"
python3 "$STANDALONE/p25_ref/p25_repro.py" tc "$REF/${TAG}_${BK}_before_tc.bin" "$REF/${TAG}_${BK}_after_tc.bin" || true
echo "[bitcheck $BK] chain sidecar"
python3 "$STANDALONE/p25_ref/p25_repro.py" chain "$REF/${TAG}_${BK}_before_ch.bin" "$REF/${TAG}_${BK}_after_ch.bin" || true
