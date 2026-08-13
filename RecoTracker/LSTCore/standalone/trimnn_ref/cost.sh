#!/bin/bash
# cost.sh <MODE> <jets|pu> <NEVT>  -- K6f / K7a per-kernel cost, single stream, LST_CHAIN_TIMING.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
A=/mnt/data1/gsn27/here/gpu_wt/tn1/src/RecoTracker/LSTCore/standalone
R=$S/trimnn_ref/runs
JETS=$S/jet_ref/trackingNtuple_jets_1000.root
PU=/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/event_1000.root
M=$1; WHAT=$2; N=$3
cd $A || exit 9
source setup.sh > /dev/null 2>&1; eval $(scramv1 runtime -sh) 2>/dev/null; source setup.sh > /dev/null 2>&1
export LD_LIBRARY_PATH=$A/LST:$LD_LIBRARY_PATH
case $WHAT in jets) I=$JETS; X=-J ;; pu) I=$PU; X= ;; esac
LST_CHAIN_TIMING=1 LST_TRIM_MODE=$M $A/bin/lst_cpu -i $I -n $N -p 0.8 -s 1 -w 0 $X -o $R/cost_${M}_${WHAT}.root > $R/cost_${M}_${WHAT}.log 2>&1
rm -f $R/cost_${M}_${WHAT}.root
python3 - "$R/cost_${M}_${WHAT}.log" "$M" "$WHAT" <<'PY'
import re, sys
import numpy as np
txt = open(sys.argv[1], errors="ignore").read()
pat = re.compile(r"chains=(\d+) weldedNodes=(\d+) \| K6ab weld ([\d.]+) ms \| K6cd count\+prefix ([\d.]+) ms \| "
                 r"K6e emit ([\d.]+) ms \| K6f trim ([\d.]+) ms \| K7a features ([\d.]+) ms \| "
                 r"K7bc gate ([\d.]+) ms \| total ([\d.]+) ms")
rows = np.array([[float(x) for x in m.groups()] for m in pat.finditer(txt)])
assert len(rows), "no CHAIN TIMING lines"
n = len(rows)
print("mode %s %s  events %d | chains/evt %.0f | K6f trim %.3f ms | K7a features %.3f ms | "
      "K7bc gate %.3f ms | weld-block total %.3f ms"
      % (sys.argv[2], sys.argv[3], n, rows[:, 0].mean(), rows[:, 5].mean(), rows[:, 6].mean(),
         rows[:, 7].mean(), rows[:, 8].mean()))
PY
