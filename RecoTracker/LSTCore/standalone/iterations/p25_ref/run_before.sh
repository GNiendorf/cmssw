#!/bin/bash
# P2.5 timing BEFORE leg. Reverts ONLY the P2.5 hunks (p25_changes.patch), rebuilds, measures the
# same stage-level chain timing the AFTER leg measured, then restores the patch and rebuilds so the
# tree and the binaries end where they started.
#
# The two pre-existing uncommitted files (interface/alpaka/LST.h, src/alpaka/LST.cc, the P2.4
# chainConfig plumbing) are NOT touched: the patch names only the ten files P2.5 changed.
set -e
CORE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore
STANDALONE="$CORE/standalone"
REF="$STANDALONE/p25_ref"

cd "$CORE"
echo "[before] reverting the P2.5 hunks"
git apply -R --check "$REF/p25_changes.patch"
git apply -R "$REF/p25_changes.patch"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

echo "[before] rebuilding the baseline"
lst_make_tracklooper -mcCGd > /dev/null 2>&1
if [ ! -x bin/lst_cpu ] || [ ! -x bin/lst_cuda ]; then
  echo "[before] BASELINE BUILD FAILED - restoring the patch and stopping"
  cd "$CORE" && git apply "$REF/p25_changes.patch"
  exit 1
fi

echo "[before] timing legs"
bash "$REF/run_timing.sh" before cpu  20 > "$REF/out_timing_before.txt"  2>&1
bash "$REF/run_timing.sh" before cuda 20 >> "$REF/out_timing_before.txt" 2>&1

# Attribution legs that only the baseline binary can produce.
#  (i)  gate (a) with the OLD tie-break: if it is still an exact match against the frozen
#       prototype, the P2.5 gate-(a) mismatch is the tie-break swap and nothing else.
#  (ii) the baseline chain sidecar on the same 10 events the P2.5 audit leg used, so the two chain
#       sets can be diffed directly instead of through the track candidates.
echo "[before] gate (a) with the baseline binary"
bash "$REF/run_gate_a.sh" 10 > "$REF/out_gate_a_before.txt" 2>&1 || true
echo "[before] baseline chain sidecar, 10 events"
rm -f "$REF/base_ch10.bin"
LST_CHAIN_CHAIN_DUMP="$REF/base_ch10.bin" lst_cpu -i PU200RelVal -n 10 -s 1 -w 0 --use_chain_tracking \
  -o "$REF/base_ch10.root" > "$REF/base_ch10.log" 2>&1 || true

echo "[before] restoring the P2.5 hunks"
cd "$CORE"
git apply "$REF/p25_changes.patch"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
echo "[before] rebuilding the P2.5 tree"
lst_make_tracklooper -mcCGd > /dev/null 2>&1
ls -la bin/lst_cpu bin/lst_cuda

echo BEFORE_DONE
