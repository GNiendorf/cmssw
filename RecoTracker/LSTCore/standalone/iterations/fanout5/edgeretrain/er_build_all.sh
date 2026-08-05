#!/bin/bash
# er_build_all.sh <tag> [<tag> ...]
# Exports edge_mlp_weights.h from each variant, rebuilds, parks bin/chainproto_<tag>.
# Restores the GOLDEN v3 header + rebuilds bin/chainproto at the end, so the tree's
# default binary always equals the golden one.
set -e
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout5/edgeretrain"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
cd "$P"
cp -f "$S/fanout4/compose_attach/edge_mlp_weights.h" "$P/edge_mlp_weights.h.golden"
for TAG in "$@"; do
  echo "=== building $TAG ==="
  python3 export_weights.py --model "$P/er_edge_mlp_${TAG}.pt" \
    --norm "$P/er_edge_norm_${TAG}.json" --out "$P/edge_mlp_weights.h"
  cp -f "$P/edge_mlp_weights.h" "$P/edge_mlp_weights_${TAG}.h"
  make -j 24 > "$P/build_${TAG}.log" 2>&1
  cp -f bin/chainproto "bin/chainproto_${TAG}"
  echo "  -> bin/chainproto_${TAG}"
done
cp -f "$P/edge_mlp_weights.h.golden" "$P/edge_mlp_weights.h"
make -j 24 > "$P/build_restore.log" 2>&1
echo "golden header restored; bin/chainproto is the v3 (golden) build"
md5sum "$P/edge_mlp_weights.h" "$S/fanout4/compose_attach/edge_mlp_weights.h"
