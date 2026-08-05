#!/bin/bash
# P2.4b-1 leg P: the bare-T3 stage-B physics dump, on the pinned probe build.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/p24b_ref"
N="${1:-60}"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
export LD_LIBRARY_PATH="$R/bin_probe:$LD_LIBRARY_PATH"
LST_CHAIN_T3ATTACH=1 LST_CHAIN_T3_HIST="$R/phys_hist.bin" \
  "$R/bin_probe/lst_cpu" -i PU200RelVal -n "$N" -s 1 --allobj --use_chain_tracking \
  -o "$R/phys.root" > "$R/phys.log" 2>&1
echo "exit=$?"
