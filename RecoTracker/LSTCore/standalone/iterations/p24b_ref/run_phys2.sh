#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/p24b_ref"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
export LD_LIBRARY_PATH="$R/bin_repl:$LD_LIBRARY_PATH"
for cc in 1 0; do
  LST_CHAIN_T3ATTACH=1 LST_CHAIN_T3_MAXCLAIMED=$cc \
    "$R/bin_repl/lst_cpu" -i PU200RelVal -n 60 -s 1 --allobj --use_chain_tracking \
    -o "$R/phys_c${cc}.root" > "$R/phys_c${cc}.log" 2>&1
  echo "c$cc exit=$?"
done
