#!/bin/bash
# er_build_variant.sh <tag>
# Re-exports edge_mlp_weights.h from er_edge_mlp_<tag>.pt / er_edge_norm_<tag>.json,
# rebuilds, and parks the binary as bin/chainproto_<tag>. The GOLDEN v3 header is
# restored afterwards so the tree's default state always matches the golden tree.
set -e
TAG="$1"
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout5/edgeretrain"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
cd "$P"
cp -f "$S/fanout4/compose_attach/edge_mlp_weights.h" "$P/edge_mlp_weights.h.golden"
python3 export_weights.py --model "$P/er_edge_mlp_${TAG}.pt" --norm "$P/er_edge_norm_${TAG}.json" \
  --out "$P/edge_mlp_weights.h"
make -j 16 > "$P/build_${TAG}.log" 2>&1
cp -f bin/chainproto "bin/chainproto_${TAG}"
md5sum "$P/edge_mlp_weights.h" "bin/chainproto_${TAG}"
# restore golden header + golden binary
cp -f "$P/edge_mlp_weights.h.golden" "$P/edge_mlp_weights.h"
make -j 16 >> "$P/build_${TAG}.log" 2>&1
echo "built bin/chainproto_${TAG}; golden header restored"
