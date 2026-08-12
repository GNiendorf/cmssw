#!/bin/bash
# M1JG1 -- THE JET GATE, GPU. `-n 100 -s 1` on the first 100 jet events.
#
# The GPU is the backend where the ceiling is a POLICY limit rather than a device limit: the CMS
# caching allocator refuses anything over binGrowth^maxBin = 1 GiB, and m1_ref/base100.py shows
# TWELVE of the first 100 jet events want more than that for their edge rows (5, 25, 28, 42, 64, 71,
# 74, 81, 85, 91, 92, 97) -- so the L40's 46 GB is irrelevant and no GPU jet run of any length
# exists today. Arm BASE proves that on the untouched 81a9afe2d00 binary in this same slot rather
# than by citation.
#
# The cap sweep matters more here than on the CPU: the question the GPU asks is not "how fast" (JR:
# the whole jet sample is 0.72 ns/edge, a non-event) but "which C makes the 1 GiB bin unreachable on
# every event". 0 / 256 / 512 / 128, and 256 again at the end so the arms have an in-slot floor.
# NO `set -u`: standalone/setup.sh legitimately reads unset variables, and with nounset a
# sourced setup.sh aborts the whole job with status 1 and its message swallowed by the
# redirect -- which is exactly how the first submission of this script died, silently.
M=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R=$M/m1_ref
B=$R/frozenbase
V=$R/frozenvar
G=/mnt/data1/gsn27/here/gpu_wt/g2/src/RecoTracker/LSTCore/standalone
J=$M/jet_ref/trackingNtuple_jets_1000.root
cd $G || exit 9
source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

echo "=== FROZEN BINARY PROOF, BOTH TREES (void on mismatch) ==="
for D in $B $V; do ( cd $D && md5sum -c MD5 ) || { echo "*** MD5 MISMATCH in $D -- VOID ***"; exit 1; }; done

smon () {  # sample GPU 0 used MiB until the sentinel disappears
  while [ -f $1.on ]; do
    nvidia-smi --query-gpu=memory.used --format=csv,noheader,nounits | head -1 >> $1
    sleep 0.2
  done
}

arm () {   # arm <label> <dir> <cap>
  local lbl=$1 D=$2 cap=$3
  rm -f $R/jg1_$lbl.smi; touch $R/jg1_$lbl.smi.on
  smon $R/jg1_$lbl.smi & local mpid=$!
  local t0=$SECONDS
  /usr/bin/time -v env LST_CHAIN_DEG_CAP=$cap LD_LIBRARY_PATH=$D:$LD_LIBRARY_PATH \
      $D/lst_cuda -i $J -n 100 -s 1 -v 1 -w 0 > $R/jg1_$lbl.log 2> $R/jg1_$lbl.err
  local rc=$?
  rm -f $R/jg1_$lbl.smi.on; wait $mpid 2>/dev/null
  echo "  arm $lbl ($(basename $D), cap=$cap) rc=$rc wall $((SECONDS-t0)) s"
  echo -n "    avg line: "; grep -E '^\s+avg' $R/jg1_$lbl.log | tail -1
  echo    "    per-event timing rows: $(grep -cE '^ +[0-9]+ +[0-9]' $R/jg1_$lbl.log)"
  echo    "    [CHAIN OVERFLOW] skips: $(grep -h -c 'CHAIN OVERFLOW' $R/jg1_$lbl.log $R/jg1_$lbl.err | awk '{s+=$1} END {print s}')"
  grep -h 'CHAIN OVERFLOW' $R/jg1_$lbl.log $R/jg1_$lbl.err | sed 's/^/      /' | head -14
  [ $rc -ne 0 ] && { echo "    --- the failure, verbatim ---"; tail -6 $R/jg1_$lbl.err | sed 's/^/      /'; }
  echo    "    peak device MiB (nvidia-smi, GPU 0, 5 Hz): $(sort -n $R/jg1_$lbl.smi 2>/dev/null | tail -1)"
  echo    "    peak host RSS: $(( $(grep 'Maximum resident' $R/jg1_$lbl.err | tr -dc '0-9') / 1024 )) MB"
}

echo
echo "=== ARM BASE: the UNTOUCHED 81a9afe2d00 binary. Expected: dies, no run exists. ==="
arm base $B 0

echo
echo "=== ARMS on the patched binary ==="
arm off  $V 0
arm c256 $V 256
arm c512 $V 512
arm c128 $V 128
arm c256b $V 256

echo
echo "=== per-event distributions, off -> c256 paired ==="
python3 $R/perev.py $R/jg1_off.log $R/jg1_c256.log
echo
python3 $R/perev.py $R/jg1_c512.log $R/jg1_c128.log
echo
echo "=== in-slot repeatability floor: c256 vs c256b ==="
python3 $R/perev.py $R/jg1_c256.log $R/jg1_c256b.log
echo DONE M1JG1
