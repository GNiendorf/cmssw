#!/bin/bash
# P2.6d JOB 1: the 300-event physics scoreboard, P2.5 state vs HEAD state, both toolchains pinned.
# usage: run_scoreboard.sh <cpu|cuda> [streams]
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
PROTO="$STANDALONE/prototype"
REF="$STANDALONE/p26d_ref"
BK="${1:-cpu}"
S="${2:-32}"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

set -e
for leg in p25 head; do
  D="$REF/bin_${leg}_d"
  echo "[scoreboard/$BK] $leg, 300 events, -s $S"
  rm -f "$REF/sb_${BK}_${leg}.root" "$REF/sb_${BK}_${leg}_hists.root" "$REF/sb_${BK}_${leg}.json"
  LD_LIBRARY_PATH="$D:$LD_LIBRARY_PATH" "$D/lst_$BK" -i PU200RelVal -n 300 -s "$S" --use_chain_tracking \
    -o "$REF/sb_${BK}_${leg}.root" > "$REF/sb_${BK}_${leg}.log" 2>&1
  createPerfNumDenHists -i "$REF/sb_${BK}_${leg}.root" -o "$REF/sb_${BK}_${leg}_hists.root" \
    >> "$REF/sb_${BK}_${leg}.log" 2>&1
  python3 "$PROTO/compare_ab.py" --proto "$REF/sb_${BK}_${leg}_hists.root" --base "$PROTO/base300_hists.root" \
    --json "$REF/sb_${BK}_${leg}.json" > /dev/null
done

echo
echo "################ $BK : HEAD vs P2.5 (must be 29/29 identical) ################"
python3 "$STANDALONE/p25_ref/p25_scoreboard.py" "$REF/sb_${BK}_p25.json" "$REF/sb_${BK}_head.json"
echo "SCOREBOARD_DONE $BK"
