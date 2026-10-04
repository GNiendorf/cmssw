#!/bin/bash
# replay_throughput.sh <outdir> [modes] [streams list] [extra cmsRun args...]   (harness lane; run in a CMSSW environment)
#   modes: comma list of inputs,stock,port (default inputs,stock); streams: comma list (default 1,4,8); threads = 8
#   extra args go to test/replay_throughput_cfg.py, e.g. maxEvents=2000 cache=50 fit=1 accelerators=gpu-nvidia
#   portCff=RecoTracker.MkFitAlpaka.mkFitAlpakaChain_cff portSeq=mkFitAlpakaChain sample=qcd
# Prints, per (mode, streams): events/s, the chain CPU ms/event summed from FastTimerService over the modules the
# inputs job does not run, and the chain cost per event relative to mode=inputs at the same streams:
#   cost_ms = 1000 * threads * (1/rate_mode - 1/rate_inputs)   (CPU-bound: all threads busy; on GPU read the rate)
# Sequential jobs only (2 cmsRun jobs at most on the box; measure on a quiet box).
OUT=$(readlink -f $1); MODES=${2:-inputs,stock}; STREAMS=${3:-1,4,8}; shift 3 2>/dev/null; EXTRA="$@"
CFG=$(dirname $(readlink -f $0))/replay_throughput_cfg.py
THREADS=8
mkdir -p $OUT && cd $OUT || exit 1
declare -A RATE
for s in ${STREAMS//,/ }; do
  for m in ${MODES//,/ }; do
    tag=${m}_s$s
    cmsRun $CFG mode=$m threads=$THREADS streams=$s timingJson=$OUT/$tag.json $EXTRA > $tag.log 2>&1 || { echo "FAIL $tag"; continue; }
    RATE[$tag]=$(grep -o "Average throughput: [0-9.e+]*" $tag.log | awk '{print $3}')
  done
done
# chain CPU per event from FastTimerService: modules of the mode's job that the inputs job does not have
chain_cpu() {
  python3 -c "
import json, sys
m = json.load(open(sys.argv[1])); b = json.load(open(sys.argv[2]))
base = {x['label'] for x in b['modules']}
mods = [x for x in m['modules'] if x['label'] not in base and x['type'] not in ('PathStatusInserter', 'EndPathStatusInserter')]
print('%.1f' % (sum(x['time_thread'] for x in mods) / max(1, m['total']['events'])))" "$1" "$2" 2>/dev/null || echo -
}
{ printf "%-8s %7s %10s %12s %16s\n" mode streams "ev/s" "cost ms/ev" "chain cpu ms/ev"
for s in ${STREAMS//,/ }; do
  for m in ${MODES//,/ }; do
    r=${RATE[${m}_s$s]}; r0=${RATE[inputs_s$s]}
    c=$(python3 -c "r=float('${r:-nan}'); r0=float('${r0:-nan}'); print('%.1f' % (1000*$THREADS*(1/r-1/r0)) if r0==r0 and r==r and '$m'!='inputs' else '-')")
    cc=-; [ $m != inputs ] && cc=$(chain_cpu $OUT/${m}_s$s.json $OUT/inputs_s$s.json)
    printf "%-8s %7s %10s %12s %16s\n" $m $s "${r:-?}" "$c" "$cc"
  done
done; } | tee summary.txt
