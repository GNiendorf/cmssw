#!/bin/bash
# run_py.sh <script.py> <outfile> [args...]
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1 && cmsenv > /dev/null 2>&1 && source setup.sh > /dev/null 2>&1
SC="$1"; shift
OUT="$1"; shift
python3 "$SC" "$@" > "$OUT" 2>&1
echo "done -> $OUT"
