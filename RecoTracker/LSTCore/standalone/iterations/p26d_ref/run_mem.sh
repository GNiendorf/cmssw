#!/bin/bash
# P2.6d JOB 2: the per-event allocation table, from the production build's own [MEM] lines
# (-v 2 turns objectsStatistics_ on). 10 events, CPU, one log per configuration.
# usage: run_mem.sh <bindir> [nevents]
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p26d_ref"
BIND="$REF/${1:?bindir}"
N="${2:-10}"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

set -e
for CFG in base hybrid preview; do
  FLAG=""; ENVV=""
  [ "$CFG" != "base" ] && FLAG="--use_chain_tracking"
  [ "$CFG" == "preview" ] && ENVV="LST_CHAIN_SKIP_DOOMED=1"
  echo "[mem] $CFG"
  env $ENVV LD_LIBRARY_PATH="$BIND:$LD_LIBRARY_PATH" \
    "$BIND/lst_cpu" -i PU200RelVal -n "$N" -s 1 -v 2 -w 0 $FLAG \
    -o "$REF/mem_${CFG}.root" > "$REF/mem_${CFG}.log" 2>&1
done
echo MEM_RUNS_DONE
