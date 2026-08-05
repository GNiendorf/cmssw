#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1 && cmsenv > /dev/null 2>&1 && source setup.sh > /dev/null 2>&1
OUT="$1"; shift
python3 "$S/a06_ref/a06_decomp.py" "$@" > "$OUT" 2>&1
echo "decomp done -> $OUT"
