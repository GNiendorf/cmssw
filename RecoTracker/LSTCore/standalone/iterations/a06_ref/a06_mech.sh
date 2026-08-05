#!/bin/bash
# Per-tag mechanism ledger: trim volume and extension volume straight out of the run log.
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/a06_ref
for t in "$@"; do
  f="$P/r_${t}.log"
  [ -f "$f" ] || f="/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fin_ref/r_${t}.log"
  [ -f "$f" ] || { echo "$t: no log"; continue; }
  tr=$(grep -m1 "B2 terminal trim" "$f")
  ex=$(grep -m1 "EX chain extend" "$f")
  echo "== $t"
  echo "   ${tr:-  (trim off)}"
  echo "   ${ex:-  (extend off)}"
done
