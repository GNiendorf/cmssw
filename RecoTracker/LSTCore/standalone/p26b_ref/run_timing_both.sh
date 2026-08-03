#!/bin/bash
# Sequential timing legs, CPU then CUDA, never in parallel.
# usage: run_timing_both.sh <tag> [nevents]
REF=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/p26b_ref
TAG="${1:?tag}"
N="${2:-20}"
"$REF/run_timing.sh" "$TAG" cpu "$N"
"$REF/run_timing.sh" "$TAG" cuda "$N"
echo "DONE_TIMING_BOTH $TAG"
