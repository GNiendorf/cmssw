#!/bin/bash
# P2.6d: build the tree in its current git state and snapshot the FULL toolchain
# (executables AND liblst_*.so, because the algorithm lives in the library).
#
# usage: build_snap.sh <destdir-name> <make-flags e.g. -md>
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p26d_ref"
DEST="$REF/${1:?destdir}"
FLAGS="${2:--md}"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

echo "[build_snap] git: $(git rev-parse --short HEAD)  flags: $FLAGS"
BEFORE=$(ls -1 "$STANDALONE"/.make.log.* 2>/dev/null | tail -1)
lst_make_tracklooper $FLAGS > "$REF/$(basename $DEST)_build.stdout" 2>&1
RC=$?
AFTER=$(ls -1 "$STANDALONE"/.make.log.* 2>/dev/null | tail -1)
echo "[build_snap] rc=$RC  fresh log: $AFTER (was $BEFORE)"
if [ "$AFTER" == "$BEFORE" ]; then echo "[build_snap] FAIL: no fresh make log"; exit 1; fi
grep -E "^ERROR|Error [0-9]|error:" "$AFTER" | head -20
grep -cE "^ERROR|Error [0-9]|error:" "$AFTER" | sed 's/^/[build_snap] error-line count: /'
# A CPU-only build (-mCd) leaves no lst_cuda / liblst_cuda.so, so snapshot whatever this build
# actually produced and only insist that at least one complete backend pair is present.
mkdir -p "$DEST"
GOT=0
for bk in cpu cuda; do
  if [ -f "$STANDALONE/bin/lst_$bk" ] && [ -f "$STANDALONE/LST/liblst_$bk.so" ]; then
    cp -p "$STANDALONE/bin/lst_$bk" "$STANDALONE/LST/liblst_$bk.so" "$DEST/"
    GOT=$((GOT+1))
  fi
done
[ $GOT -gt 0 ] || { echo "[build_snap] FAIL: no complete backend pair produced"; exit 1; }
cp -p "$STANDALONE/code/rooutil/librooutil.so" "$DEST/" 2>/dev/null || true
md5sum "$DEST"/* | tee "$REF/md5_$(basename $DEST).txt"
echo "BUILD_SNAP_DONE $(basename $DEST)"
