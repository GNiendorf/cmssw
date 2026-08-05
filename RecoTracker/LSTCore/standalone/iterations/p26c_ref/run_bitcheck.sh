#!/bin/bash
# P2.6c gate (a): 10-event --allobj bit-identity of the CURRENT build against the pristine P2.6b
# reference in p26c_ref/bin_before, on one backend.
#
# IMPORTANT (P2.6c finding): lst_cpu / lst_cuda are NOT self-contained. They dynamically link
# LST/liblst_<backend>.so, which is where every kernel and all of LSTEvent live. Snapshotting only
# the executable therefore does NOT snapshot the algorithm: an old executable run after a rebuild
# picks up the NEW library and either crashes (if a layout changed) or silently measures the new
# code against itself. p26c_ref/bin_before holds BOTH the executables and the .so files, and the
# BEFORE leg below pins LD_LIBRARY_PATH at that directory so it really runs the old algorithm.
#
# usage: run_bitcheck.sh <cpu|cuda> [nevents] [tag]
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p26c_ref"
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

echo "[bitcheck $BK] leg BEFORE (p26c_ref/bin_before, own libs pinned)"
LD_LIBRARY_PATH="$REF/bin_before:$LD_LIBRARY_PATH" \
LST_CHAIN_TC_DUMP="$REF/${TAG}_${BK}_before_tc.bin" LST_CHAIN_CHAIN_DUMP="$REF/${TAG}_${BK}_before_ch.bin" \
  "$REF/bin_before/lst_$BK" -i PU200RelVal -n "$N" -s 1 --allobj --use_chain_tracking \
  -o "$REF/${TAG}_${BK}_before.root" > "$REF/${TAG}_${BK}_before.log" 2>&1

echo "[bitcheck $BK] leg AFTER (bin/lst_$BK)"
LST_CHAIN_TC_DUMP="$REF/${TAG}_${BK}_after_tc.bin" LST_CHAIN_CHAIN_DUMP="$REF/${TAG}_${BK}_after_ch.bin" \
  lst_$BK -i PU200RelVal -n "$N" -s 1 --allobj --use_chain_tracking \
  -o "$REF/${TAG}_${BK}_after.root" > "$REF/${TAG}_${BK}_after.log" 2>&1

# Positive control: the two legs must be provably different builds. P2.6c changed the incidence
# keying, so with -v 2 the BEFORE log says "MD keys" and the AFTER log "dense MD keys". At -v 0 no
# [MEM] line is printed, so fall back to comparing the two binaries' build identity by size+mtime.
echo "[bitcheck $BK] positive control (legs are distinct builds)"
cmp -s "$REF/bin_before/lst_$BK" "$STANDALONE/bin/lst_$BK" \
  && echo "  CONTROL FAIL: BEFORE and AFTER executables are byte-identical" \
  || echo "  ok: BEFORE and AFTER executables differ"
cmp -s "$REF/bin_before/liblst_$BK.so" "$STANDALONE/LST/liblst_$BK.so" \
  && echo "  CONTROL FAIL: BEFORE and AFTER libraries are byte-identical" \
  || echo "  ok: BEFORE and AFTER libraries differ"

echo "[bitcheck $BK] ntuple branch-by-branch"
python3 "$STANDALONE/p20_bitcheck.py" "$REF/${TAG}_${BK}_before.root" "$REF/${TAG}_${BK}_after.root" || true
echo "[bitcheck $BK] TC sidecar"
python3 "$STANDALONE/p25_ref/p25_repro.py" tc "$REF/${TAG}_${BK}_before_tc.bin" "$REF/${TAG}_${BK}_after_tc.bin" || true
echo "[bitcheck $BK] chain sidecar"
python3 "$STANDALONE/p25_ref/p25_repro.py" chain "$REF/${TAG}_${BK}_before_ch.bin" "$REF/${TAG}_${BK}_after_ch.bin" || true
