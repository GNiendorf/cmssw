#!/bin/bash
# P2.4b-1 stage-B cost on the GPU (and the matching CPU leg), sequential by construction.
# LST_CHAIN_TIMING drains the queue around every stage, so these are stage-attribution numbers.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/p24b_ref"
N="${1:-20}"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
export LD_LIBRARY_PATH="$R/bin_repl_cuda:$R/bin_repl:$LD_LIBRARY_PATH"

for cc in 6 1; do
  for be in cuda cpu; do
    BIN="$R/bin_repl_cuda/lst_$be"
    [ "$be" = "cpu" ] && BIN="$R/bin_repl/lst_cpu"
    echo "[gputime] $be maxClaimed=$cc"
    LST_CHAIN_TIMING=1 LST_CHAIN_T3ATTACH=1 LST_CHAIN_T3_MAXCLAIMED=$cc \
      "$BIN" -i PU200RelVal -n "$N" -s 1 -v 1 -w 0 --use_chain_tracking \
      -o "$R/gt_${be}_c${cc}.root" > "$R/gt_${be}_c${cc}.log" 2>&1
    echo "  exit=$?"
  done
done

# baseline legs (probe OFF) for the like-for-like total
for be in cuda cpu; do
  BIN="$R/bin_repl_cuda/lst_$be"
  [ "$be" = "cpu" ] && BIN="$R/bin_repl/lst_cpu"
  echo "[gputime] $be probe OFF"
  LST_CHAIN_TIMING=1 "$BIN" -i PU200RelVal -n "$N" -s 1 -v 1 -w 0 --use_chain_tracking \
    -o "$R/gt_${be}_off.root" > "$R/gt_${be}_off.log" 2>&1
  echo "  exit=$?"
done
echo "[gputime] DONE"
