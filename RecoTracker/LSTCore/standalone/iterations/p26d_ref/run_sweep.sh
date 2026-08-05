#!/bin/bash
# P2.6d JOB 2: the stream sweep, replicating the protocol of efficiency/bin/lst_timing
# (-n 200 -v 1 -w 0 -s <streams>; CPU 1,4,16,32,64; GPU 1,2,4,6,8) but WITHOUT its rebuild step,
# so that all three configurations are measured with the SAME production binary.
#
# RUNS ARE STRICTLY SEQUENTIAL. Never start anything else on this machine while it runs.
#
# usage: run_sweep.sh <cpu|cuda> <base|hybrid|preview> <bindir> [nevents] [sample]
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p26d_ref"
BK="${1:?backend}"
CFG="${2:?config}"
BIND="$REF/${3:?bindir}"
N="${4:-200}"
SAMPLE="${5:-PU200RelVal}"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

CHAINFLAG=""
SKIPENV=""
case "$CFG" in
  base)    CHAINFLAG="" ;;
  hybrid)  CHAINFLAG="--use_chain_tracking" ;;
  preview) CHAINFLAG="--use_chain_tracking"; SKIPENV="LST_CHAIN_SKIP_DOOMED=1" ;;
  *) echo "bad config $CFG"; exit 1 ;;
esac

if [ "$BK" == "cpu" ]; then STREAMS="1 4 16 32 64"; else STREAMS="1 2 4 6 8"; fi

OUT="$REF/sweep_${BK}_${CFG}.log"
rm -f "$OUT"
echo "# sweep backend=$BK config=$CFG bindir=$BIND n=$N sample=$SAMPLE" > "$OUT"
md5sum "$BIND/lst_$BK" "$BIND/liblst_$BK.so" >> "$OUT"
for S in $STREAMS; do
  echo "[sweep] $BK $CFG -s $S"
  echo "=================== streams $S ===================" >> "$OUT"
  env $SKIPENV LD_LIBRARY_PATH="$BIND:$LD_LIBRARY_PATH" \
    "$BIND/lst_$BK" -i "$SAMPLE" -n "$N" -v 1 -w 0 -s "$S" $CHAINFLAG \
    -o "$REF/sweep_${BK}_${CFG}_s${S}.root" >> "$OUT" 2>&1
  rm -f "$REF/sweep_${BK}_${CFG}_s${S}.root"
done
echo "SWEEP_DONE $BK $CFG"
grep -h "^   avg" "$OUT" | tail -20
