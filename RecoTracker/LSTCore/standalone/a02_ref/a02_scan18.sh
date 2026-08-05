#!/bin/bash
# a02_scan18.sh <t1> [<t2> ...]
# One 300-event harness run per SHARP-LABEL dedup-head threshold. Assembled baseline
# line with the single substitution -XCH 1; -XCT keeps its role as THE one tuned
# constant, on the head's scale. Tags are N<thr> so the M13 batch (H<thr>, the
# old-label head) stays on the scoreboard for comparison.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
for t in "$@"; do
  tag="N$(echo "$t" | tr -d '.-' | tr '[:lower:]' '[:upper:]')"
  setsid nohup bash "$S/a02_ref/a02_run.sh" "$tag" \
    -XC 3 -XCT "$t" -T3E 1 -CC 1 -CCN 1 -CCR 2 -XCH 1 \
    > "$S/a02_ref/nohup_$tag.out" 2>&1 < /dev/null &
  disown
  echo "launched $tag  (-XCH 1 -XCT $t)"
  sleep 2
done
