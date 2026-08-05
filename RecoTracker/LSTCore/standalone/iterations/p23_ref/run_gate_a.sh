#!/bin/bash
# P2.3 gate (a): TC-level parity, production flag-ON vs the frozen prototype on the same events.
#
#   leg B  production flag-OFF, --allobj   -> the LST ntuple the prototype consumes (and the
#                                             OFF-state bit-identity subject)
#   leg P  prototype on leg B with the frozen-minus-attach command, PROTO_DUMP_TCHITS=1
#                                          -> the impersonated tc_type / tc_hitOT multiset
#   leg A  production flag-ON,  --allobj, LST_CHAIN_TC_DUMP -> the ported tc multiset
#
# Usage: run_gate_a.sh [nevents]
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
PROTO="$STANDALONE/prototype"
REF="$STANDALONE/p23_ref"
TRKDIR=/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/
N="${1:-10}"

source "$REF/frozen_flags.sh"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

set -e
rm -f "$REF/ga_off.root" "$REF/ga_on.root" "$REF/ga_proto.root" "$REF/ga_prod.bin"

echo "[gate a] leg B: production flag-OFF, $N events"
lst_cpu -i PU200RelVal -n "$N" -s 1 --allobj -o "$REF/ga_off.root" > "$REF/ga_off.log" 2>&1

echo "[gate a] leg P: prototype (frozen minus attach) on leg B"
PROTO_DUMP_TCHITS=1 "$PROTO/bin/chainproto" -m hybrid -i "$REF/ga_off.root" -t "$TRKDIR" \
  -o "$REF/ga_proto.root" "${FROZEN_FLAGS[@]}" > "$REF/ga_proto.log" 2>&1
tail -12 "$REF/ga_proto.log"

echo "[gate a] leg A: production flag-ON, $N events"
LST_CHAIN_TC_DUMP="$REF/ga_prod.bin" lst_cpu -i PU200RelVal -n "$N" -s 1 --allobj --use_chain_tracking \
  -o "$REF/ga_on.root" > "$REF/ga_on.log" 2>&1
grep -h "CHAIN K9" "$REF/ga_on.log" | tail -3

echo "[gate a] compare"
python3 "$REF/p23_compare_tc.py" "$REF/ga_proto.root" "$REF/ga_prod.bin"
