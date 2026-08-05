#!/bin/bash
# a15_scan.sh -- launch a batch of 300-evt points in parallel.
#   a15_scan.sh <batchname> "<TAG1>|<overrides1>" "<TAG2>|<overrides2>" ...
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
BATCH="$1"; shift
BASE="-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2"
for spec in "$@"; do
  TAG="${spec%%|*}"
  OV="${spec#*|}"
  bash "$S/a15_ref/a15_run.sh" "$TAG" $BASE $OV &
done
wait
echo "[a15] BATCH $BATCH DONE"
