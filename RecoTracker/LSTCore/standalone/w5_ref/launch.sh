#!/bin/bash
# W5: launch the weld-replay reduction over <first>..<last> with N slots.
#   usage: launch.sh <first> <last> <nslots> <jet|pu>
FIRST=${1:-0}; LAST=${2:-63}; N=${3:-16}; S=${4:-pu}
SA=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
TOT=$((LAST - FIRST + 1))
PER=$(( (TOT + N - 1) / N ))
for s in $(seq 0 $((N-1))); do
  a=$((FIRST + s*PER)); b=$((a + PER - 1))
  if [ $a -gt $LAST ]; then break; fi
  if [ $b -gt $LAST ]; then b=$LAST; fi
  setsid nohup bash $SA/w5_ref/run_wr.sh $a $b ${S}$s $S > $SA/w5_ref/slot_${S}$s.log 2>&1 &
done
wait
echo ALLDONE $S $FIRST..$LAST
