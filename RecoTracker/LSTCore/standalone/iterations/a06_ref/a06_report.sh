#!/bin/bash
# a06_report.sh <tag> [tag...]  -- headline table, per-region table, mechanism ledger.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1 && cmsenv > /dev/null 2>&1 && source setup.sh > /dev/null 2>&1
echo "=== A. HEADLINE (300 evts) ==="
python3 "$S/a06_ref/a06_tab.py" "$@"
echo
echo "=== B. PER REGION ==="
python3 "$S/a06_ref/a06_tab.py" -r "$@"
echo
echo "=== C. MECHANISM LEDGER ==="
bash "$S/a06_ref/a06_mech.sh" "$@"
