#!/bin/bash
# gen_round.sh <chunkA> <chunkB> -- run two 250-event chunks of the instrumented 1000-event
# regeneration in parallel (64 streams each = 128 of 192 threads) with a stall watchdog.
# Usage: gen_round.sh 0 1
# NOTE: no `set -u` here -- setup.sh dereferences unset variables and would abort the script.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
G=$S/rebase_ref/gen
NEV=1000
NJOB=4
STALL=1200   # kill a job whose output file has not grown for this many seconds
GRACE=1500   # no stall check before this many seconds have elapsed

mkdir -p "$G"
pushd "$S" > /dev/null || exit 1
source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

declare -a PIDS OUTS TAGS
for I in "$@"; do
  OUT="$G/LSTNtuple_instr_1000evt_c${I}.root"
  rm -f "$OUT"
  "$S/bin/lst_cpu" -i PU200RelVal --allobj -n $NEV -s 64 -p 0.8 -v 1 \
      -j $NJOB -I "$I" -o "$OUT" > "$G/c${I}.log" 2>&1 &
  P=$!
  PIDS+=("$P")
  OUTS+=("$OUT")
  TAGS+=("c$I")
  echo "[gen] launched chunk $I pid $P -> $OUT"
done

T0=$(date +%s)
declare -a LASTSIZE LASTGROW
for k in "${!PIDS[@]}"; do LASTSIZE[$k]=0; LASTGROW[$k]=$T0; done

while :; do
  alive=0
  now=$(date +%s)
  for k in "${!PIDS[@]}"; do
    if kill -0 "${PIDS[$k]}" 2>/dev/null; then
      alive=1
      sz=$(stat -c %s "${OUTS[$k]}" 2>/dev/null || echo 0)
      if [ "$sz" -gt "${LASTSIZE[$k]}" ]; then
        LASTSIZE[$k]=$sz
        LASTGROW[$k]=$now
      fi
      idle=$(( now - LASTGROW[$k] ))
      elapsed=$(( now - T0 ))
      if [ "$elapsed" -gt "$GRACE" ] && [ "$idle" -gt "$STALL" ]; then
        echo "[gen] WATCHDOG: ${TAGS[$k]} pid ${PIDS[$k]} stalled ${idle}s at ${LASTSIZE[$k]} bytes -- KILLING"
        kill -9 "${PIDS[$k]}" 2>/dev/null
        echo "STALLED after ${elapsed}s, ${LASTSIZE[$k]} bytes" > "$G/${TAGS[$k]}.STALL"
      fi
    fi
  done
  [ "$alive" -eq 0 ] && break
  sleep 30
done

T1=$(date +%s)
echo "[gen] round done in $((T1 - T0))s"
for k in "${!PIDS[@]}"; do
  printf "  %s  %s bytes  %s\n" "${TAGS[$k]}" "$(stat -c %s "${OUTS[$k]}" 2>/dev/null || echo MISSING)" \
    "$( [ -f "$G/${TAGS[$k]}.STALL" ] && echo STALLED || echo OK )"
done
echo "WALL_SECONDS $((T1 - T0))" >> "$G/round_$(echo "$@" | tr ' ' '_').time"
