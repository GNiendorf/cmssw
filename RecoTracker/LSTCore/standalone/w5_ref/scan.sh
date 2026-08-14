#!/bin/bash
# W5: run a batch of weld arms.   usage: scan.sh <armfile> [parallel] [which]
# armfile lines:  TAG TIE KNEE CAL SWEEPS FAMOFF   ('-' = leave unset)
O=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
AF=$1; P=${2:-6}; WHICH=${3:-jet}
mkdir -p $O/w5_ref/logs
i=0
while read -r TAG TM KN CAL SW FO; do
  [ -z "$TAG" ] && continue
  case "$TAG" in \#*) continue;; esac
  (
    unset LST_CHAIN_WELD_TIE LST_CHAIN_WELD_KNEE LST_CHAIN_WELD_CAL LST_CHAIN_WELD_SWEEPS LST_CHAIN_WELD_FAMOFF
    [ "$TM" != "-" ] && export LST_CHAIN_WELD_TIE=$TM
    [ "$KN" != "-" ] && export LST_CHAIN_WELD_KNEE=$KN
    [ -n "$CAL" ] && [ "$CAL" != "-" ] && export LST_CHAIN_WELD_CAL=$CAL
    [ -n "$SW" ] && [ "$SW" != "-" ] && export LST_CHAIN_WELD_SWEEPS=$SW
    [ -n "$FO" ] && [ "$FO" != "-" ] && export LST_CHAIN_WELD_FAMOFF=$FO
    bash $O/w5_ref/gates.sh $TAG $WHICH > $O/w5_ref/logs/$TAG.$WHICH.out 2>&1
  ) &
  i=$((i+1))
  if [ $((i % P)) -eq 0 ]; then wait; fi
done < $AF
wait
echo "SCAN DONE $WHICH $(date -Is)"
