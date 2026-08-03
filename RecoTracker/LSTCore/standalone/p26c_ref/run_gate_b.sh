#!/bin/bash
# P2.6c gate (b): the 300-event physics scoreboard, BEFORE and AFTER, on one backend.
#
# The reference row set is regenerated from the PRE-CHANGE binaries in p26_ref/bin_before, so the
# comparison isolates P2.6c from every earlier phase; the frozen-prototype leg (p24_ref/p24freeze.json)
# stays available as the absolute reference.
#
# usage: run_gate_b.sh <cpu|cuda> [streams]
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
PROTO="$STANDALONE/prototype"
REF="$STANDALONE/p26c_ref"
BK="${1:-cpu}"
S="${2:-32}"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

set -e
for leg in before after; do
  rm -f "$REF/sb_${BK}_${leg}.root" "$REF/sb_${BK}_${leg}_hists.root" "$REF/sb_${BK}_${leg}.json"
done

echo "[gate b/$BK] BEFORE binary, 300 events"
LD_LIBRARY_PATH="$REF/bin_before:$LD_LIBRARY_PATH" "$REF/bin_before/lst_$BK" -i PU200RelVal -n 300 -s "$S" --use_chain_tracking \
  -o "$REF/sb_${BK}_before.root" > "$REF/sb_${BK}_before.log" 2>&1
createPerfNumDenHists -i "$REF/sb_${BK}_before.root" -o "$REF/sb_${BK}_before_hists.root" \
  >> "$REF/sb_${BK}_before.log" 2>&1
python3 "$PROTO/compare_ab.py" --proto "$REF/sb_${BK}_before_hists.root" --base "$PROTO/base300_hists.root" \
  --json "$REF/sb_${BK}_before.json" > /dev/null

echo "[gate b/$BK] AFTER binary, 300 events"
"lst_$BK" -i PU200RelVal -n 300 -s "$S" --use_chain_tracking \
  -o "$REF/sb_${BK}_after.root" > "$REF/sb_${BK}_after.log" 2>&1
createPerfNumDenHists -i "$REF/sb_${BK}_after.root" -o "$REF/sb_${BK}_after_hists.root" \
  >> "$REF/sb_${BK}_after.log" 2>&1
python3 "$PROTO/compare_ab.py" --proto "$REF/sb_${BK}_after_hists.root" --base "$PROTO/base300_hists.root" \
  --json "$REF/sb_${BK}_after.json" > /dev/null

echo
echo "################ $BK : P2.6c AFTER vs BEFORE (must be 29/29 identical) ################"
python3 "$STANDALONE/p25_ref/p25_scoreboard.py" "$REF/sb_${BK}_before.json" "$REF/sb_${BK}_after.json"
echo
echo "################ $BK : P2.6c AFTER vs the FROZEN prototype (p24freeze) ################"
python3 "$STANDALONE/p25_ref/p25_scoreboard.py" "$STANDALONE/p24_ref/p24freeze.json" "$REF/sb_${BK}_after.json"
