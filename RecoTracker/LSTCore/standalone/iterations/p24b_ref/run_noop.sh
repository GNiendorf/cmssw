#!/bin/bash
# P2.4b-1 no-op gate: with LST_CHAIN_T3ATTACH / LST_CHAIN_T3REPLACE unset, the measurement build
# must produce a bit-identical ntuple to the PRISTINE tree binary pinned by P2.6d
# (p26d_ref/bin_prod_pristine, built from the same HEAD this session started on).
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/p24b_ref"
N="${1:-30}"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

echo "[noop] leg PRISTINE"
LD_LIBRARY_PATH="$S/p26d_ref/bin_prod_pristine:$LD_LIBRARY_PATH" \
  "$S/p26d_ref/bin_prod_pristine/lst_cpu" -i PU200RelVal -n "$N" -s 1 --use_chain_tracking \
  -o "$R/noop_pristine.root" > "$R/noop_pristine.log" 2>&1
echo "  exit=$?"

echo "[noop] leg NEW (flags unset)"
LD_LIBRARY_PATH="$R/bin_repl:$LD_LIBRARY_PATH" \
  "$R/bin_repl/lst_cpu" -i PU200RelVal -n "$N" -s 1 --use_chain_tracking \
  -o "$R/noop_new.root" > "$R/noop_new.log" 2>&1
echo "  exit=$?"

echo "[noop] bit compare"
python3 "$S/p20_bitcheck.py" "$R/noop_pristine.root" "$R/noop_new.root"
echo "  bitcheck exit=$?"
