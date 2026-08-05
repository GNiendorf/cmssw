#!/bin/bash
# P2.4b-1 TASK 6 -- the pT3-class REPLACEMENT A/B scoreboard, 300 PU200RelVal events.
#
#   b0        : the CURRENT state -- chain tracking on, LST's own pT3 rows carried (the baseline)
#   r<TT>[c<C>] : LST_CHAIN_T3REPLACE=1 -- the bare-T3 attach DELIVERS the pT3 class and LST's own
#               pT3 rows are retired wholesale, at class margin -AT3 = TT/10 and at most C of the
#               target T3's six hits already claimed by an accepted chain (C omitted = 6 = the
#               frozen, unfiltered M16 universe).
#
# Every leg goes through the IDENTICAL harness the P2.4 gate (b) used: createPerfNumDenHists then
# prototype/compare_ab.py against prototype/base300_hists.root, so every JSON is row-comparable to
# every JSON any earlier phase produced.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/p24b_ref"
P="$S/prototype"
N="${N:-300}"
BIN="$R/bin_repl/lst_cpu"

pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
export LD_LIBRARY_PATH="$R/bin_repl:$LD_LIBRARY_PATH"

leg() { # $1 = tag, $2.. = env assignments
  local tag="$1"; shift
  echo "[board] $tag : $*"
  env "$@" "$BIN" -i PU200RelVal -n "$N" -s 32 --use_chain_tracking \
    -o "$R/bd_${tag}.root" > "$R/bd_${tag}.log" 2>&1
  echo "  run exit=$?"
  createPerfNumDenHists -i "$R/bd_${tag}.root" -o "$R/bd_${tag}_hists.root" >> "$R/bd_${tag}.log" 2>&1
  echo "  hists exit=$?"
  python3 "$P/compare_ab.py" --proto "$R/bd_${tag}_hists.root" --base "$P/base300_hists.root" \
    --json "$R/bd_${tag}.json" > "$R/bd_${tag}_cmp.txt" 2>&1
  echo "  compare exit=$?"
  rm -f "$R/bd_${tag}.root"   # 300-event ntuples are large and the hists file is the artefact
}

for spec in "$@"; do
  if [ "$spec" = "b0" ]; then
    leg b0 LST_CHAIN_T3REPLACE=0
    continue
  fi
  body="${spec#r}"
  case "$body" in
    *c*) tt="${body%c*}"; cc="${body#*c}" ;;
    *)   tt="$body";      cc=6 ;;
  esac
  th=$(python3 -c "print($tt/10.0)")
  leg "$spec" LST_CHAIN_T3REPLACE=1 LST_CHAIN_T3_THETA="$th" LST_CHAIN_T3_MAXCLAIMED="$cc"
done
echo "[board] ALL DONE"
