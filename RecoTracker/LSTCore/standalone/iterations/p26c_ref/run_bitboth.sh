#!/bin/bash
# Gate (a) on both backends, sequentially. usage: run_bitboth.sh <tag> [nevents]
REF=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/p26c_ref
TAG="${1:?tag}"
N="${2:-10}"
"$REF/run_bitcheck.sh" cpu  "$N" "$TAG"
echo "=================================================================="
"$REF/run_bitcheck.sh" cuda "$N" "$TAG"
echo "DONE_BITBOTH $TAG"
