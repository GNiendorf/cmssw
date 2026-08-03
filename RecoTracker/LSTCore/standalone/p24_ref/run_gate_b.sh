#!/bin/bash
# P2.4 gate (b): the FULL-FREEZE 300-event physics scoreboard.
#
#   reference : prototype/freeze_verify_hists.root -- the frozen prototype (FULL frozen command,
#               attach live) over LSTNtuple_PU200RelVal_300evt.root, the M19 freeze artefact
#   ported    : production flag-ON over the same 300 PU200RelVal events
# Both go through the identical harness (createPerfNumDenHists + compare_ab.py vs the LST baseline
# prototype/base300_hists.root), so the two JSONs are directly comparable row for row.
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
PROTO="$STANDALONE/prototype"
REF="$STANDALONE/p24_ref"
TAG="${1:-p24prod300}"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

set -e
rm -f "$REF/${TAG}.root" "$REF/${TAG}_hists.root"
echo "[gate b] production flag-ON, 300 events"
lst_cpu -i PU200RelVal -n 300 -s 32 --use_chain_tracking -o "$REF/${TAG}.root" > "$REF/${TAG}.log" 2>&1
echo "[gate b] createPerfNumDenHists"
createPerfNumDenHists -i "$REF/${TAG}.root" -o "$REF/${TAG}_hists.root" >> "$REF/${TAG}.log" 2>&1
echo "[gate b] compare_ab.py vs the LST baseline"
python3 "$PROTO/compare_ab.py" --proto "$REF/${TAG}_hists.root" --base "$PROTO/base300_hists.root" --json "$REF/${TAG}.json"
echo "[gate b] REFERENCE leg (frozen prototype freeze_verify_hists.root) through the same harness"
python3 "$PROTO/compare_ab.py" --proto "$PROTO/freeze_verify_hists.root" --base "$PROTO/base300_hists.root" --json "$REF/p24freeze.json"
