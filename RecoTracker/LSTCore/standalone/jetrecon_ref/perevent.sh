#!/bin/bash
# Per-event isolated runs so a crash in one event does not lose the rest.
# usage: perevent.sh <input> <first> <last> <tag> [backend]
set -u
IN=$1; FIRST=$2; LAST=$3; TAG=$4; BE=${5:-cpu}
cd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
OUT=jetrecon_ref/$TAG
mkdir -p $OUT
for i in $(seq $FIRST $LAST); do
  /usr/bin/time -v ./bin/lst_$BE -i "$IN" -x $i -n -1 -s 1 -v 2 -w 0 \
      > $OUT/evt$i.log 2> $OUT/evt$i.err
  echo "evt $i rc=$?"
done
