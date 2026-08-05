#!/bin/bash
# P2.6d: localise the CPU chains.score drift to one of the three optimisation phases.
# Builds CPU-only debug toolchains for P2.6a and P2.6b and snapshots them. The tree MUST be clean
# (the P2.6d skip patch has to be stashed first); it is left back on chain_tracking_proto.
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p26d_ref"
SRC=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src

cd "$SRC"
set -e
for pair in "28d4d861c00 bin_p26a_d" "483a8a62725 bin_p26b_d"; do
  set -- $pair
  SHA=$1; DEST=$2
  echo "[bisect] checkout $SHA -> $DEST"
  git checkout "$SHA" > /dev/null 2>&1
  "$REF/build_snap.sh" "$DEST" -mCd > "$REF/build_${DEST}.out" 2>&1
  tail -3 "$REF/build_${DEST}.out"
  cp -p "$STANDALONE/code/rooutil/librooutil.so" "$REF/$DEST/" 2>/dev/null || true
done
git checkout chain_tracking_proto > /dev/null 2>&1
echo "[bisect] tree back on $(git rev-parse --short HEAD)"
cp -p "$STANDALONE/code/rooutil/librooutil.so" "$REF/bin_p25_d/" 2>/dev/null || true
cp -p "$STANDALONE/code/rooutil/librooutil.so" "$REF/bin_head_d/" 2>/dev/null || true
echo BISECT_BUILDS_DONE
