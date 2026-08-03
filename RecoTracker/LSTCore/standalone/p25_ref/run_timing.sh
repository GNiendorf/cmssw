#!/bin/bash
# P2.5 stage-level chain timing, one leg. Run the two legs SEQUENTIALLY, never in parallel.
# usage: run_timing.sh <tag> <backend: cpu|cuda> [nevents]
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p25_ref"
TAG="${1:?tag}"
BK="${2:-cpu}"
N="${3:-20}"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

set -e
rm -f "$REF/t_${TAG}_${BK}.log"
LST_CHAIN_TIMING=1 "lst_$BK" -i PU200RelVal -n "$N" -s 1 -w 0 --use_chain_tracking \
  -o "$REF/t_${TAG}_${BK}.root" > "$REF/t_${TAG}_${BK}.log" 2>&1
echo "wrote $REF/t_${TAG}_${BK}.log"
grep -c "CHAIN TIMING" "$REF/t_${TAG}_${BK}.log"
