#!/bin/bash
# a06_post.sh <tag> [tag...] -- redo the harness + judge for runs whose chainproto stage
# finished but whose post-processing was interrupted.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/a06_ref"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1 && cmsenv > /dev/null 2>&1 && source setup.sh > /dev/null 2>&1
for t in "$@"; do
  [ -f "$P/r_${t}.root" ] || { echo "$t: no root"; continue; }
  # createPerfNumDenHists refuses to overwrite; a killed run can leave a stub behind.
  rm -f "$P/r_${t}_hists.root"
  createPerfNumDenHists -i "$P/r_${t}.root" -o "$P/r_${t}_hists.root" >> "$P/r_${t}.log" 2>&1
  python3 "$S/prototype/compare_ab.py" --proto "$P/r_${t}_hists.root" \
     --base "$S/rebase_ref/rb_base300_hists.root" --json "$P/r_${t}.json" \
     > "$P/r_agg_${t}.txt" 2>/dev/null
  echo "post $t done"
done
