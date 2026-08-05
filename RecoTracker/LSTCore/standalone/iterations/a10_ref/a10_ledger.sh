#!/bin/bash
# a10_ledger.sh <tag> [<tag> ...] -- per-event delivery ledger pulled out of the run logs.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
printf "%-20s %9s %9s %9s %9s %9s %9s %9s\n" tag chainAtt T3deliv carried chainTC totTC RDrevoke CCrevoke
for t in "$@"; do
  L=""
  for d in "$S/a10_ref" "$S/fin_ref" "$S/xc_ref"; do
    [ -f "$d/r_${t}.log" ] && L="$d/r_${t}.log" && break
  done
  [ -z "$L" ] && { printf "%-20s MISSING\n" "$t"; continue; }
  ca=$(grep -o 'chain-attached=[0-9]* mean=[0-9.]*' "$L" | sed 's/.*mean=//')
  t3=$(grep -o 'T3-attached=[0-9]* mean=[0-9.]*' "$L" | sed 's/.*mean=//')
  rd=$(grep -o 'seed-family dedup revoked=[0-9]* mean=[0-9.]*' "$L" | sed 's/.*mean=//')
  cc=$(grep -o 'OT side .*: revoked=[0-9]* ([0-9.]*/evt)' "$L" | sed 's#.*(\([0-9.]*\)/evt)#\1#')
  ln=$(grep 'output TCs/evt' "$L")
  tot=$(echo "$ln" | sed 's/.*mean=\([0-9.]*\) .*/\1/')
  pk=$(echo "$ln"  | sed 's/.*pixel kept \([0-9.]*\) .*/\1/')
  sp=$(echo "$ln"  | sed 's/.*suppressed \([0-9.]*\) .*/\1/')
  ch=$(echo "$ln"  | sed 's/.*chain \([0-9.]*\) .*/\1/')
  carr=$(python3 -c "print('%.1f'%($pk-$sp))" 2>/dev/null)
  printf "%-20s %9s %9s %9s %9s %9s %9s %9s\n" "$t" "$ca" "$t3" "$carr" "$ch" "$tot" "$rd" "$cc"
done
