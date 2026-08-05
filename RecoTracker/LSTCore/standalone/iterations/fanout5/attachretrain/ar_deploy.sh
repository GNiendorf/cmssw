#!/bin/bash
# ar_deploy.sh <variant>  -- export a trained M19 attach head into the C++ build and
# prove the port is bit-faithful before any A/B number is taken.
#
#   1. export_attach_weights.py  attach_mlp_<v>.pt + attach_norm_<v>.json
#                                -> attach_mlp_weights.h   (the header whose EXISTENCE
#                                   flips AttachInference.cc off the sentinel path)
#   2. make                      rebuild bin/chainproto against the new header
#   3. tools/attach_parity       C++ attachLogit over a deterministic row selection
#      attach_parity_gen.py      the SAME rows recomputed from the torch checkpoint
#                                -> must agree to < 1e-3 per target kind, BOTH kinds
#
# The parity step is not ceremony: the exporter transposes the weight matrices and bakes
# the clip/log10_1p conditioning into the header by hand, so a silent mismatch there
# would show up only as a mysteriously worse scoreboard.
V="$1"
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout5/attachretrain"

pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
cd "$P" || exit 1

echo "=== [1/3] export $V ==="
python3 export_attach_weights.py --model "$P/attach_mlp_${V}.pt" \
  --norm "$P/attach_norm_${V}.json" --out "$P/attach_mlp_weights.h" || exit 1

echo "=== [2/3] rebuild ==="
make -j 16 > "$P/build_${V}.log" 2>&1 || { echo "BUILD FAILED"; tail -20 "$P/build_${V}.log"; exit 1; }
cp "$P/bin/chainproto" "$P/bin/chainproto_${V}"
echo "build ok -> bin/chainproto_${V}"

echo "=== [3/3] parity ==="
g++ $(root-config --cflags) -O2 -std=c++17 "$P/tools/attach_parity.cc" "$P/AttachInference.o" \
  $(root-config --libs) -I"$P" -o "$P/tools/attach_parity" || exit 1
"$P/tools/attach_parity" "$P/dump/pr_c00.root" "$P/tools/parity_cpp_${V}.json" 1000 || exit 1
python3 attach_parity_gen.py --model "$P/attach_mlp_${V}.pt" --norm "$P/attach_norm_${V}.json" \
  --input "$P/dump/pr_c00.root" --out "$P/tools/parity_ref_${V}.json" \
  --cpp "$P/tools/parity_cpp_${V}.json"
echo "PARITY EXIT=$?"
