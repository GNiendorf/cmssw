#!/bin/bash
# P2.5 race check: the same events at two different stream counts must give the same output.
# Event RECORD order in the sidecars follows completion order at -s > 1, so the comparison is
# pooled over all events (p25_repro.py chainpool / tcpool).
#
# usage: run_streams.sh <backend: cpu|cuda> <nevents>
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p26b_ref"
BK="${1:-cpu}"
N="${2:-10}"
EXE="lst_$BK"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

set -e
for s in 1 4; do
  rm -f "$REF/str_${BK}_s${s}.root" "$REF/str_${BK}_s${s}_ch.bin" "$REF/str_${BK}_s${s}_tc.bin"
  echo "[streams] $BK -s $s"
  LST_CHAIN_CHAIN_DUMP="$REF/str_${BK}_s${s}_ch.bin" LST_CHAIN_TC_DUMP="$REF/str_${BK}_s${s}_tc.bin" \
    "$EXE" -i PU200RelVal -n "$N" -s "$s" --allobj --use_chain_tracking \
    -o "$REF/str_${BK}_s${s}.root" > "$REF/str_${BK}_s${s}.log" 2>&1
done

echo
echo "################ $BK  -s 1  vs  -s 4 ################"
python3 "$STANDALONE/p25_ref/p25_repro.py" chainpool "$REF/str_${BK}_s1_ch.bin" "$REF/str_${BK}_s4_ch.bin" || true
python3 "$STANDALONE/p25_ref/p25_repro.py" tcpool    "$REF/str_${BK}_s1_tc.bin" "$REF/str_${BK}_s4_tc.bin" || true
