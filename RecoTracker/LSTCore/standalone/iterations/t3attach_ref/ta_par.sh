#!/bin/bash
# ta_par.sh <specfile>   -- run every line "<tag> <overrides...>" of <specfile> in
# parallel through ta_run.sh, then echo ALLDONE_<basename>. Blank lines and '#' ignored.
cd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/t3attach_ref
SPEC="$1"
while read -r line; do
  case "$line" in ''|'#'*) continue;; esac
  # shellcheck disable=SC2086
  ( bash ta_run.sh $line > /dev/null 2>&1; echo "done ${line%% *}" ) &
done < "$SPEC"
wait
echo "ALLDONE_$(basename "$SPEC" .txt)"
