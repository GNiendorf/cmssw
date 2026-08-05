#!/bin/bash
# a02_scan.sh <t1> [<t2> ...]
# Fire one 300-event harness run per dedup-head threshold, all from the ASSEMBLED
# BASELINE line with the single substitution -XCH 1 (the bare-chain dedup criterion
# becomes the purpose-built head). -XCT keeps its role as THE one tuned constant, now on
# the head's scale.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
for t in "$@"; do
  tag="H$(echo "$t" | tr -d '.-' | tr '[:lower:]' '[:upper:]')"
  setsid nohup bash "$S/a02_ref/a02_run.sh" "$tag" \
    -XC 3 -XCT "$t" -T3E 1 -CC 1 -CCN 1 -CCR 2 -XCH 1 \
    > "$S/a02_ref/nohup_$tag.out" 2>&1 < /dev/null &
  disown
  echo "launched $tag  (-XCH 1 -XCT $t)"
  sleep 1
done
