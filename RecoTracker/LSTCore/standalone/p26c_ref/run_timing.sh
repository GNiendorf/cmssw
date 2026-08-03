#!/bin/bash
# P2.6c stage-level chain timing, one leg. Run the legs SEQUENTIALLY, never in parallel.
#
# Same library-pinning caveat as run_bitcheck.sh: the algorithm lives in LST/liblst_<bk>.so, so the
# "before" leg must run the executable AND the library from p26c_ref/bin_before.
#
# usage: run_timing.sh <tag> <backend: cpu|cuda> <leg: before|after> [nevents]
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p26c_ref"
TAG="${1:?tag}"
BK="${2:-cpu}"
LEG="${3:-after}"
N="${4:-20}"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

set -e
OUT="$REF/t_${TAG}_${BK}_${LEG}"
rm -f "$OUT.log" "$OUT.root"

if [ "$LEG" == "before" ]; then
  LD_LIBRARY_PATH="$REF/bin_before:$LD_LIBRARY_PATH" LST_CHAIN_TIMING=1 \
    "$REF/bin_before/lst_$BK" -i PU200RelVal -n "$N" -s 1 -w 0 --use_chain_tracking \
    -o "$OUT.root" > "$OUT.log" 2>&1
else
  LST_CHAIN_TIMING=1 "lst_$BK" -i PU200RelVal -n "$N" -s 1 -w 0 --use_chain_tracking \
    -o "$OUT.root" > "$OUT.log" 2>&1
fi
echo "wrote $OUT.log  ([CHAIN TIMING] lines: $(grep -c 'CHAIN TIMING' "$OUT.log"))"
