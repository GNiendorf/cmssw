#!/bin/bash
# P2.4b-1 leg T: stage-B cost, measured against the SAME build with the probe off.
# Sequential by construction -- never run two of these at once.
#
#   $1 = backend (cpu|cuda)   $2 = nevents (default 30)
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/p24b_ref"
B="${1:-cpu}"
N="${2:-30}"
BIN="$R/bin_probe_$B/lst_$B"

pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
export LD_LIBRARY_PATH="$R/bin_probe_$B:$LD_LIBRARY_PATH"

echo "[T] $B baseline (probe OFF), $N events"
"$BIN" -i PU200RelVal -n "$N" -s 1 -v 1 -w 0 --use_chain_tracking \
  -o "$R/t_${B}_off.root" > "$R/t_${B}_off.log" 2>&1
echo "  exit=$?"

echo "[T] $B probe ON, $N events"
LST_CHAIN_T3ATTACH=1 "$BIN" -i PU200RelVal -n "$N" -s 1 -v 1 -w 0 --use_chain_tracking \
  -o "$R/t_${B}_on.root" > "$R/t_${B}_on.log" 2>&1
echo "  exit=$?"

for tag in off on; do
  echo "--- $B $tag ---"
  grep -E "^\s*avg" "$R/t_${B}_${tag}.log" | tail -2
  tail -4 "$R/t_${B}_${tag}.log" | head -3
done
