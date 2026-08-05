#!/bin/bash
# er_build_combo.sh <edgetag> <gatetag> <outtag>
# Builds a binary with BOTH the retrained edge head and the retrained 3-class chain gate,
# parked as bin/chainproto_<outtag>. Golden headers restored at the end.
set -e
ET="$1"; GT="$2"; OT="$3"
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout5/edgeretrain"
G="$S/fanout4/compose_attach"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
cd "$P"
python3 export_weights.py --model "$P/er_edge_mlp_${ET}.pt" \
  --norm "$P/er_edge_norm_${ET}.json" --out "$P/edge_mlp_weights.h"
python3 export_chain3_weights.py --model "$P/chain3_mlp_${GT}.pt" \
  --norm "$P/chain3_norm_${GT}.json" --out "$P/chain3_mlp_weights.h"
make -j 24 > "$P/build_${OT}.log" 2>&1
cp -f bin/chainproto "bin/chainproto_${OT}"
cp -f "$G/edge_mlp_weights.h" "$P/edge_mlp_weights.h"
cp -f "$G/chain3_mlp_weights.h" "$P/chain3_mlp_weights.h"
make -j 24 > "$P/build_restore.log" 2>&1
echo "built bin/chainproto_${OT} (edge=${ET} gate=${GT}); golden headers restored"
md5sum "$P/edge_mlp_weights.h" "$P/chain3_mlp_weights.h"
