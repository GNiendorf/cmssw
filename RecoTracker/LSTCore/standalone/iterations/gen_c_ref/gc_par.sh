#!/bin/bash
# gc_par.sh <specfile> -- run every "<tag> <overrides...>" line in parallel.
cd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
while read -r line; do
  case "$line" in ''|'#'*) continue;; esac
  ( bash gen_c_ref/gc_run.sh $line > /dev/null 2>&1; echo "done ${line%% *}" ) &
done < "$1"
wait
echo "ALLDONE_$(basename "$1" .txt)"
